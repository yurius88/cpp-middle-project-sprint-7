#include "headers.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ranges>
#include <stdexcept>

using namespace std::string_view_literals;

namespace {

constexpr std::string_view kLineDelimiter = "\r\n";
constexpr std::string_view kHeaderValueSeparator = ": ";
constexpr std::string_view kHostHeader = "Host";
constexpr std::string_view kContentLengthHeader = "Content-Length";
constexpr std::string_view kDefaultPort = "80";

std::string_view trim_helper(std::string_view value) {
    constexpr std::string_view whitespace = " \t";
    const auto begin = value.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

bool isNameEquals(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs,
                              [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

}  // namespace

void iterHeaders(std::string_view req, Callback &&callback) {
    auto lines = req | std::views::split(kLineDelimiter);

    auto it = lines.begin();
    const auto end = lines.end();
    if (it == end) {
        return;
    }

    ++it;

    for (; it != end; ++it) {
        const auto line_range = *it;
        std::string_view line(line_range.begin(), line_range.end());
        if (line.empty()) {
            break;
        }

        const auto separator_pos = line.find(':');
        if (separator_pos == std::string_view::npos) {
            continue;
        }

        const std::string_view name = trim_helper(line.substr(0, separator_pos));
        const std::string_view value = trim_helper(line.substr(separator_pos + 1));
        if (name.empty()) {
            continue;
        }

        callback(name, value);
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::optional<std::string_view> host_value;

    iterHeaders(req, [&host_value](std::string_view name, std::string_view value) {
        if (isNameEquals(name, kHostHeader)) {
            host_value = value;
        }
    });

    if (!host_value.has_value()) {
        throw std::runtime_error("Host header is missing");
    }

    const auto colon_pos = host_value->find(':');
    if (colon_pos == std::string_view::npos) {
        return {std::string(*host_value), std::string(kDefaultPort)};
    }

    return {std::string(host_value->substr(0, colon_pos)), std::string(host_value->substr(colon_pos + 1))};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;

    iterHeaders(rsp, [&result](std::string_view name, std::string_view value) {
        if (!isNameEquals(name, kContentLengthHeader)) {
            return;
        }

        size_t parsed = 0;
        const auto parse_result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (parse_result.ec == std::errc{}) {
            result = parsed;
        }
    });

    return result;
}
