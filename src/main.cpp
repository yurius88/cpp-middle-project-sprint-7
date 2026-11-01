#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>

using boost::asio::async_connect;
using boost::asio::async_read;
using boost::asio::async_read_until;
using boost::asio::async_write;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::io_service;
using boost::asio::transfer_at_least;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

constexpr std::string_view delimiter = "\r\n\r\n";

// Ограничение на размер заголовков: защищает от исчерпания памяти,
// если клиент/сервер шлёт данные без завершающего разделителя.
constexpr size_t kMaxHeaderSize = 64 * 1024;

// Размер порции для чтения тела: всё тело целиком в память не читается.
constexpr size_t kBodyChunkSize = 16 * 1024;

awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    try {
        // Читаем заголовки запроса со стороны клиента.
        std::string request;
        co_await async_read_until(client_socket, boost::asio::dynamic_buffer(request, kMaxHeaderSize),
                                  std::string{delimiter}, use_awaitable);

        const auto [host, port] = findHostPort(request);
        std::println("Proxying {}:{}", host, port);

        tcp::resolver resolver(io_service);
        const auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);

        tcp::socket server_socket(io_service);
        co_await async_connect(server_socket, endpoints, use_awaitable);

        // Пересылаем запрос клиента сайту (вместе с уже прочитанной частью тела).
        co_await async_write(server_socket, boost::asio::buffer(request), use_awaitable);

        // Читаем заголовки ответа со стороны сайта.
        std::string response;
        co_await async_read_until(server_socket, boost::asio::dynamic_buffer(response, kMaxHeaderSize),
                                  std::string{delimiter}, use_awaitable);

        const size_t header_size = response.find(delimiter) + delimiter.size();
        const size_t buffered_body = response.size() - header_size;
        const std::optional<size_t> content_length = findContentLength(response);

        // Передаём клиенту заголовки и уже прочитанную часть тела.
        co_await async_write(client_socket, boost::asio::buffer(response), use_awaitable);

        // Тело читаем порциями фиксированного размера.
        std::array<char, kBodyChunkSize> chunk;

        if (content_length.has_value()) {
            size_t remaining = content_length.value() > buffered_body ? content_length.value() - buffered_body : 0;
            while (remaining > 0) {
                const size_t want = std::min(remaining, chunk.size());
                const size_t n = co_await async_read(server_socket, boost::asio::buffer(chunk.data(), want),
                                                     transfer_at_least(1), use_awaitable);
                co_await async_write(client_socket, boost::asio::buffer(chunk.data(), n), use_awaitable);
                remaining -= n;
            }
        } else {
            // Без Content-Length передаём всё вплоть до закрытия соединения.
            for (;;) {
                error_code ec;
                const size_t n = co_await async_read(server_socket, boost::asio::buffer(chunk), transfer_at_least(1),
                                                     redirect_error(use_awaitable, ec));
                if (ec == boost::asio::error::eof) {
                    break;
                }
                if (ec) {
                    throw boost::system::system_error(ec);
                }
                co_await async_write(client_socket, boost::asio::buffer(chunk.data(), n), use_awaitable);
            }
        }
    } catch (const boost::system::system_error &e) {
        std::println("Session closed: {}", e.what());
    } catch (const std::exception &e) {
        std::println("Session error: {}", e.what());
    }
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)), socket_(io_service) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(socket_, [this](error_code ec) {
            if (!ec) {
                co_spawn(io_service_, session(std::move(socket_), io_service_), detached);
            } else {
                std::println("Accept error: {}", ec.message());
            }
            do_accept();
        });
    }

    io_service &io_service_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << std::format("Usage: {} <listen_port>\n", argv[0]);
            return 1;
        }
        io_service io_service(1);
        Server server(io_service, static_cast<short>(std::atoi(argv[1])));
        io_service.run();
    } catch (const std::exception &e) {
        std::cerr << std::format("Exception: {}\n", e.what());
    }
}
