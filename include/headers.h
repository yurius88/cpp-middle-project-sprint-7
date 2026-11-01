#pragma once

#include <functional>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback);

struct HostInfo {
    std::string host;
    std::string port;
};

HostInfo findHostPort(std::string_view req);

std::optional<size_t> findContentLength(std::string_view rsp);
