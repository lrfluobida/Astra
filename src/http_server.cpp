#include "http_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace astra {
namespace {

constexpr std::size_t kMaximumHeaderBytes = 64U * 1024U;
constexpr std::size_t kMaximumBodyBytes = 4U * 1024U * 1024U;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parse_size(const std::string& text, std::size_t& value) {
    if (text.empty()) return false;
    std::size_t result = 0;
    for (const unsigned char ch : text) {
        if (!std::isdigit(ch)) return false;
        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    value = result;
    return true;
}

bool write_all(int socket_fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t count =
            ::send(socket_fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

void respond(int socket_fd,
             int status,
             const char* status_text,
             const std::string& body,
             const char* content_type) {
    std::ostringstream header;
    header << "HTTP/1.0 " << status << ' ' << status_text << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    write_all(socket_fd, header.str() + body);
}

bool read_request(int socket_fd, std::string& body, std::string& error) {
    std::string request;
    char buffer[8192];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const ssize_t count = ::recv(socket_fd, buffer, sizeof(buffer), 0);
        if (count > 0) {
            request.append(buffer, static_cast<std::size_t>(count));
            header_end = request.find("\r\n\r\n");
            if (header_end == std::string::npos && request.size() > kMaximumHeaderBytes) {
                error = "request headers are too large";
                return false;
            }
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            error = "connection ended before request headers";
            return false;
        }
    }
    if (header_end > kMaximumHeaderBytes) {
        error = "request headers are too large";
        return false;
    }

    const std::string headers = request.substr(0, header_end);
    const std::size_t first_line_end = headers.find("\r\n");
    const std::string request_line = headers.substr(0, first_line_end);
    if (request_line.rfind("POST ", 0) != 0) {
        error = "only POST is supported";
        return false;
    }

    bool found_length = false;
    std::size_t content_length = 0;
    std::size_t line_start = first_line_end == std::string::npos
                                 ? headers.size()
                                 : first_line_end + 2U;
    while (line_start < headers.size()) {
        const std::size_t line_end = headers.find("\r\n", line_start);
        const std::string line = headers.substr(
            line_start, line_end == std::string::npos ? std::string::npos : line_end - line_start);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string name = lowercase(line.substr(0, colon));
            std::size_t value_start = colon + 1U;
            while (value_start < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[value_start]))) {
                ++value_start;
            }
            if (name == "content-length") {
                if (found_length || !parse_size(line.substr(value_start), content_length)) {
                    error = "invalid Content-Length";
                    return false;
                }
                found_length = true;
            }
        }
        if (line_end == std::string::npos) break;
        line_start = line_end + 2U;
    }
    if (!found_length || content_length > kMaximumBodyBytes) {
        error = found_length ? "request body is too large" : "Content-Length is required";
        return false;
    }

    const std::size_t body_start = header_end + 4U;
    while (request.size() - body_start < content_length) {
        const ssize_t count = ::recv(socket_fd, buffer, sizeof(buffer), 0);
        if (count > 0) {
            request.append(buffer, static_cast<std::size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            error = "connection ended before request body";
            return false;
        }
    }
    body = request.substr(body_start, content_length);
    return true;
}

void handle_connection(int socket_fd, const HttpHandler& handler) {
    timeval timeout{};
    timeout.tv_sec = 5;
    ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::string body;
    std::string error;
    if (!read_request(socket_fd, body, error)) {
        respond(socket_fd, 400, "Bad Request", "{\"roleCommandMap\":{}}",
                "application/json; charset=utf-8");
        return;
    }
    try {
        respond(socket_fd, 200, "OK", handler(body), "application/json; charset=utf-8");
    } catch (const std::exception& exception) {
        std::cerr << "request handling error: " << exception.what() << '\n';
        respond(socket_fd, 200, "OK", "{\"roleCommandMap\":{}}",
                "application/json; charset=utf-8");
    }
}

}  // namespace

int serve_http(int port, const HttpHandler& handler) {
    std::signal(SIGPIPE, SIG_IGN);
    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return 3;

    int reuse = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(server_fd, 16) < 0) {
        ::close(server_fd);
        return 3;
    }

    std::cerr << "Astra listening on 0.0.0.0:" << port << '\n';
    while (true) {
        const int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            ::close(server_fd);
            return 3;
        }
        handle_connection(client_fd, handler);
        ::close(client_fd);
    }
}

}  // namespace astra
