#include "http_server.hpp"
#include "json_io.hpp"
#include "session.hpp"
#include "strategy.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>

namespace {

Json::Value conservative_response() {
    return astra::encode_response(astra::Decision{});
}

Json::Value decide_request(astra::AgentSession& session,
                           const astra::BaselineStrategy& strategy,
                           const Json::Value& input) {
    const auto parsed = astra::parse_turn(input);
    if (!parsed.turn || !parsed.errors.empty()) return conservative_response();
    return session.handle(input, strategy.decide(*parsed.turn));
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
    const astra::BaselineStrategy strategy;
    std::string line;
    int line_number = 0;
    while (std::getline(*input, line)) {
        ++line_number;
        Json::Value request;
        std::string error;
        if (astra::parse_json(line, request, error)) {
            std::cout << astra::write_json(decide_request(session, strategy, request)) << '\n'
                      << std::flush;
        } else {
            std::cerr << "invalid replay JSON at line " << line_number << ": " << error << '\n';
            std::cout << astra::write_json(conservative_response()) << '\n' << std::flush;
        }
    }
    return 0;
}

int serve(int port) {
    astra::AgentSession session;
    const astra::BaselineStrategy strategy;
    std::mutex session_mutex;
    const auto handler = [&](const std::string& body) {
        Json::Value decision;
        Json::Value input;
        std::string error;
        if (astra::parse_json(body, input, error)) {
            std::lock_guard<std::mutex> lock(session_mutex);
            decision = decide_request(session, strategy, input);
        } else {
            std::cerr << "invalid request JSON: " << error << '\n';
            decision = conservative_response();
        }
        return astra::write_json(decision);
    };

    if (astra::serve_http(port, handler) != 0) {
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
