#include "test_support.hpp"
#include "json_io.hpp"
#include "optional.hpp"

#include <cstdint>
#include <string>

ASTRA_TEST(toolchain_preserves_utf8_json) {
    const std::string original = "阿斯特拉：保持冷静，继续前进";
    const astra::Optional<std::int64_t> score = 9007199254740991LL;

    Json::Value encoded(Json::objectValue);
    encoded["message"] = original;
    encoded["score"] = Json::Int64(*score);
    Json::Value decoded;
    std::string error;
    astra::test::require(astra::parse_json(astra::write_json(encoded), decoded, error),
                         "jsoncpp failed a valid JSON round-trip: " + error);

    astra::test::require(decoded["message"].isString() && decoded["message"].asString() == original,
                         "UTF-8 text changed during JSON round-trip");
    astra::test::require(score.has_value(), "astra::Optional lost its value");
    astra::test::require(decoded["score"].isInt64() && decoded["score"].asInt64() == *score,
                         "64-bit integer changed during JSON round-trip");
    astra::test::require(!astra::parse_json("{\"x\":1}{\"y\":2}", decoded, error),
                         "JSON parser accepted trailing content");
}

ASTRA_TEST(optional_preserves_absence_copy_move_and_reset) {
    astra::Optional<std::string> absent;
    astra::test::require(!absent && absent.value_or("fallback") == "fallback",
                         "missing value did not use fallback");
    astra::Optional<std::string> original = "value";
    auto copy = original;
    *copy = "changed";
    astra::test::require(*original == "value" && *copy == "changed", "copy aliases original");
    astra::Optional<std::string> moved(std::move(copy));
    astra::test::require(moved.has_value() && *moved == "changed", "move lost value");
    original = astra::nullopt;
    moved.reset();
    astra::test::require(original == absent && moved == absent, "reset retained presence");
    absent = "restored";
    astra::test::require(absent->size() == 8 && absent != original, "assignment lost presence");
    astra::Optional<int> zero = 0;
    astra::Optional<int> missing;
    astra::test::require(zero && *zero == 0 && zero != missing, "zero confused with missing");
    astra::Optional<bool> false_value = false;
    astra::Optional<bool> false_copy(false_value);
    astra::Optional<bool> no_bool;
    astra::Optional<bool> no_bool_copy(no_bool);
    astra::test::require(false_copy && !*false_copy && !no_bool_copy,
                         "bool copy used presence instead of the original value");
}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "";
    return astra::test::run(filter);
}
