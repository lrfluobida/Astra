#include "session.hpp"

#include <httplib.h>

#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>

namespace {

nlohmann::json conservative_response() {
    return astra::encode_response(astra::Decision{});
}

bool parse_port(const std::string& text, int& port) {
    try {
        std::size_t consumed = 0;
        const long value = std::stol(text, &consumed);
        if (consumed != text.size() || value < 1 || value > 65535) return false;
        port = static_cast<int>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int replay(const std::string& path) {
    std::ifstream file;
    std::istream* input = &std::cin;
    if (path != "-") {
        file.open(path);
        if (!file) {
            std::cerr << "cannot open replay input: " << path << '\n';
            return 2;
        }
        input = &file;
    }

    astra::AgentSession session;
    std::string line;
    int line_number = 0;
    while (std::getline(*input, line)) {
        ++line_number;
        try {
            const auto request = nlohmann::json::parse(line);
            std::cout << session.handle(request).dump() << '\n' << std::flush;
        } catch (const nlohmann::json::exception& error) {
            std::cerr << "invalid replay JSON at line " << line_number << ": " << error.what()
                      << '\n';
            std::cout << conservative_response().dump() << '\n' << std::flush;
        }
    }
    return 0;
}

int serve(int port) {
    astra::AgentSession session;
    std::mutex session_mutex;
    httplib::Server server;

    const auto handler = [&](const httplib::Request& request, httplib::Response& response) {
        nlohmann::json decision;
        try {
            const auto input = nlohmann::json::parse(request.body);
            std::lock_guard<std::mutex> lock(session_mutex);
            decision = session.handle(input);
        } catch (const nlohmann::json::exception& error) {
            std::cerr << "invalid request JSON: " << error.what() << '\n';
            decision = conservative_response();
        } catch (const std::exception& error) {
            std::cerr << "request handling error: " << error.what() << '\n';
            decision = conservative_response();
        }
        response.status = 200;
        response.set_content(decision.dump(), "application/json; charset=utf-8");
    };

    server.Post(R"(/.*)", handler);
    std::cerr << "Astra listening on 0.0.0.0:" << port << '\n';
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "failed to listen on port " << port << '\n';
        return 3;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--replay") {
        return replay(argv[2]);
    }
    if (argc == 2) {
        int port = 0;
        if (!parse_port(argv[1], port)) {
            std::cerr << "invalid port: " << argv[1] << '\n';
            return 2;
        }
        return serve(port);
    }

    std::cerr << "usage: astra <port> | astra --replay <input.jsonl>\n";
    return 2;
}
