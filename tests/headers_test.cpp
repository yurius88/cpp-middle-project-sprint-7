#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "headers.h"

using namespace std::string_view_literals;

namespace {

struct HeaderRecord {
    std::string name;
    std::string value;

    bool operator==(const HeaderRecord &) const = default;
};

std::vector<HeaderRecord> collectHeaders(std::string_view req) {
    std::vector<HeaderRecord> result;
    iterHeaders(req, [&result](std::string_view name, std::string_view value) {
        result.push_back({std::string(name), std::string(value)});
    });
    return result;
}

}  // namespace

TEST(iterHeaders, Empty) {
    const auto headers = collectHeaders("");
    EXPECT_TRUE(headers.empty());
}

TEST(iterHeaders, SkipRequestLine) {
    const auto headers = collectHeaders("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0], (HeaderRecord{"Host", "example.com"}));
}

TEST(iterHeaders, SingleHeader) {
    const auto headers = collectHeaders("HTTP/1.1 200 OK\r\nContent-Length: 42\r\n\r\n");
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0], (HeaderRecord{"Content-Length", "42"}));
}

TEST(iterHeaders, MultipleHeaders) {
    const auto headers =
        collectHeaders("GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: wget\r\nAccept: */*\r\n\r\n");
    ASSERT_EQ(headers.size(), 3u);
    EXPECT_EQ(headers[0], (HeaderRecord{"Host", "example.com"}));
    EXPECT_EQ(headers[1], (HeaderRecord{"User-Agent", "wget"}));
    EXPECT_EQ(headers[2], (HeaderRecord{"Accept", "*/*"}));
}

TEST(iterHeaders, MultipleSameHeaders) {
    const auto headers = collectHeaders("GET / HTTP/1.1\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\n");
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0], (HeaderRecord{"Set-Cookie", "a=1"}));
    EXPECT_EQ(headers[1], (HeaderRecord{"Set-Cookie", "b=2"}));
}

TEST(iterHeaders, TrimsWhitespace) {
    const auto headers = collectHeaders("GET / HTTP/1.1\r\nHost:   example.com  \r\n\r\n");
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0], (HeaderRecord{"Host", "example.com"}));
}

TEST(iterHeaders, CaseInsensitiveLookupViaFindHostPort) {
    const auto headers = collectHeaders("GET / HTTP/1.1\r\nhOsT: example.com\r\n\r\n");
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0], (HeaderRecord{"hOsT", "example.com"}));
}

TEST(findHostPort, Simple) {
    const auto [host, port] = findHostPort("GET / HTTP/1.1\r\nHost: example.com:8080\r\n\r\n");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "8080");
}

TEST(findHostPort, DefaultPort) {
    const auto [host, port] = findHostPort("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "80");
}

TEST(findHostPort, NoHost) {
    EXPECT_THROW(findHostPort("GET / HTTP/1.1\r\nUser-Agent: wget\r\n\r\n"), std::runtime_error);
}

TEST(findHostPort, EmptyRequest) { EXPECT_THROW(findHostPort(""), std::runtime_error); }

TEST(findContentLength, Simple) {
    EXPECT_EQ(findContentLength("HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\n"), 4096u);
}

TEST(findContentLength, Zero) { EXPECT_EQ(findContentLength("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"), 0u); }

TEST(findContentLength, NoContentLength) {
    EXPECT_EQ(findContentLength("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"), std::nullopt);
}

TEST(findContentLength, EmptyResponse) { EXPECT_EQ(findContentLength(""), std::nullopt); }

TEST(findContentLength, InvalidValue) {
    EXPECT_EQ(findContentLength("HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n"), std::nullopt);
}
