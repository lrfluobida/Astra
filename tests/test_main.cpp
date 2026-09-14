#include "test_support.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

ASTRA_TEST(toolchain_preserves_utf8_json) {
    const std::string original = "阿斯特拉：保持冷静，继续前进";
    const std::optional<std::int64_t> score = 9'007'199'254'740'991LL;

    const nlohmann::json encoded = {
        {"message", original},
        {"score", *score},
    };
    const auto decoded = nlohmann::json::parse(encoded.dump());

    astra::test::require(decoded.at("message").get<std::string>() == original,
                         "UTF-8 text changed during JSON round-trip");
    astra::test::require(score.has_value(), "std::optional lost its value");
    astra::test::require(decoded.at("score").get<std::int64_t>() == *score,
                         "64-bit integer changed during JSON round-trip");

    httplib::Server server;
    astra::test::require(!server.is_running(),
                         "a newly constructed HTTP server must be stopped");
}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "";
    return astra::test::run(filter);
}
