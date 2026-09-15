#include "test_support.hpp"
#include "json_io.hpp"

#include <cstdint>
#include <optional>
#include <string>

ASTRA_TEST(toolchain_preserves_utf8_json) {
    const std::string original = "阿斯特拉：保持冷静，继续前进";
    const std::optional<std::int64_t> score = 9'007'199'254'740'991LL;

    Json::Value encoded(Json::objectValue);
    encoded["message"] = original;
    encoded["score"] = Json::Int64(*score);
    Json::Value decoded;
    std::string error;
    astra::test::require(astra::parse_json(astra::write_json(encoded), decoded, error),
                         "jsoncpp failed a valid JSON round-trip: " + error);

    astra::test::require(decoded["message"].isString() && decoded["message"].asString() == original,
                         "UTF-8 text changed during JSON round-trip");
    astra::test::require(score.has_value(), "std::optional lost its value");
    astra::test::require(decoded["score"].isInt64() && decoded["score"].asInt64() == *score,
                         "64-bit integer changed during JSON round-trip");
    astra::test::require(!astra::parse_json("{\"x\":1}{\"y\":2}", decoded, error),
                         "JSON parser accepted trailing content");
}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "";
    return astra::test::run(filter);
}
