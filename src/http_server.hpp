#pragma once

#include <functional>
#include <string>

namespace astra {

using HttpHandler = std::function<std::string(const std::string&)>;

int serve_http(int port, const HttpHandler& handler);

}  // namespace astra
