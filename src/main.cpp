#include "http_server.hpp"
#include "json_io.hpp"
#include "match_log.hpp"
#include "session.hpp"
#include "strategy.hpp"

#include <chrono>
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
    return session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        return strategy.decide(turn);
    });
}

Json::Value decide_and_log(astra::AgentSession& session,
                           const astra::BaselineStrategy& strategy,
                           astra::MatchLogger& logger,
                           const Json::Value& input) {
    const astra::SessionDiagnostics before = session.diagnostics();
    const auto started = std::chrono::steady_clock::now();
    Json::Value response = decide_request(session, strategy, input);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);
    try {
        logger.log_round(input, response, before, session.diagnostics(), elapsed);
    } catch (const std::exception& error) {
        std::cerr << "match logging failed: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "match logging failed: unknown error\n";
    }
    return response;
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
    astra::MatchLogger logger(std::cerr);
    std::string line;
    int line_number = 0;
    while (std::getline(*input, line)) {
        ++line_number;
        Json::Value request;
        std::string error;
        if (astra::parse_json(line, request, error)) {
            std::cout << astra::write_json(decide_and_log(session, strategy, logger, request)) << '\n'
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
    astra::MatchLogger logger(std::cerr);
    std::mutex session_mutex;
    const auto handler = [&](const std::string& body) {
        Json::Value decision;
        Json::Value input;
        std::string error;
        if (astra::parse_json(body, input, error)) {
            std::lock_guard<std::mutex> lock(session_mutex);
            decision = decide_and_log(session, strategy, logger, input);
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
