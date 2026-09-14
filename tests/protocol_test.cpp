#include "protocol.hpp"
#include "navigation.hpp"
#include "test_support.hpp"

#include <fstream>
#include <string>

namespace {

nlohmann::json load_fixture() {
    std::ifstream input("tests/fixtures/minimal_turn.json");
    astra::test::require(input.good(), "minimal_turn.json must be readable");
    return nlohmann::json::parse(input);
}

const astra::UnitObservation& find_unit(const astra::TurnObservation& turn, int id) {
    for (const auto& unit : turn.team_our.roles) {
        if (unit.id == id) {
            return unit;
        }
    }
    throw std::runtime_error("unit not found: " + std::to_string(id));
}

}  // namespace

ASTRA_TEST(protocol_parses_minimal_turn_and_preserves_raw_input) {
    const auto input = load_fixture();
    const auto result = astra::parse_turn(input);

    astra::test::require(result.errors.empty(), "minimal observation must parse cleanly");
    astra::test::require(result.turn.has_value(), "minimal observation must produce a turn");
    astra::test::require(result.turn->round_no == 1, "round number must be preserved");
    astra::test::require(result.turn->map.width == 41 && result.turn->map.height == 32,
                         "map dimensions must be preserved");
    astra::test::require(result.turn->team_our.team_name == "Astra",
                         "team name must be preserved");
    astra::test::require(result.turn->team_our.gold == 75, "gold must be preserved");
    astra::test::require(result.turn->world_news.official_news.find("Astra 合成测试") !=
                             std::string::npos,
                         "Chinese world news must be preserved");
    astra::test::require(result.turn->raw == input, "unmodelled input must remain in raw JSON");
    const auto station_cells = astra::occupied_cells(find_unit(*result.turn, 10013));
    const auto has_station_cell = [&](astra::Pos expected) {
        for (const auto& cell : station_cells) {
            if (cell.x == expected.x && cell.y == expected.y) return true;
        }
        return false;
    };
    astra::test::require(station_cells.size() == 4 && has_station_cell({10, 24}) &&
                             has_station_cell({11, 24}) && has_station_cell({10, 25}) &&
                             has_station_cell({11, 25}),
                         "station pos must be interpreted as the 2x2 top-left coordinate");
}

ASTRA_TEST(protocol_rejects_missing_position_without_inventing_origin) {
    auto input = load_fixture();
    input["teamOur"]["roles"][1].erase("pos");

    const auto result = astra::parse_turn(input);

    astra::test::require(result.turn.has_value(), "a malformed unit must not erase the full turn");
    astra::test::require(!result.errors.empty(), "missing unit position must report an error");
    for (const auto& unit : result.turn->team_our.roles) {
        astra::test::require(unit.id != 10010,
                             "unit with missing position must not appear at an invented coordinate");
    }
}

ASTRA_TEST(protocol_keeps_unknown_role_non_controllable_and_ignores_unknown_fields) {
    auto input = load_fixture();
    input["futureTopLevelField"] = {"enabled", true};
    input["teamOur"]["roles"].push_back({
        {"id", 10999},
        {"pos", {{"x", 4}, {"y", 5}}},
        {"roleType", "futureDrone"},
        {"health", 10},
    });

    const auto result = astra::parse_turn(input);

    astra::test::require(result.turn.has_value(), "unknown fields must not break parsing");
    const auto& unknown = find_unit(*result.turn, 10999);
    astra::test::require(unknown.role_type == astra::RoleType::unknown,
                         "unknown role type must remain unknown");
    astra::test::require(!unknown.controllable(), "unknown role type must not be controllable");
    astra::test::require(result.turn->raw.contains("futureTopLevelField"),
                         "unknown field must remain available in raw JSON");
}

ASTRA_TEST(protocol_represents_missing_documented_values_as_unknown) {
    auto input = load_fixture();
    input["robot"]["roles"].push_back({
        {"id", 30001},
        {"pos", {{"x", 4}, {"y", 4}}},
        {"roleType", "smallRobot"},
        {"health", 40},
        {"abnormalState", ""},
    });

    const auto result = astra::parse_turn(input);

    astra::test::require(result.turn.has_value(), "observation must parse");
    astra::test::require(!find_unit(*result.turn, 10013).cooldown.has_value(),
                         "missing cooldown must remain unknown");
    astra::test::require(!result.turn->robots.front().target_team.has_value(),
                         "missing robot targetTeam must remain unknown");
    astra::test::require(!result.turn->team_our.player_tasks.front().timeout_rounds.has_value(),
                         "missing timeoutRounds must remain unknown");
}

ASTRA_TEST(protocol_encodes_empty_and_attack_responses) {
    astra::Decision empty;
    astra::test::require(astra::encode_response(empty) ==
                             nlohmann::json{{"roleCommandMap", nlohmann::json::object()}},
                         "empty decision must encode as a valid empty response");

    astra::RoleCommand attack;
    attack.action = "attack";
    attack.controller_id = "10010";
    attack.target_positions = {{29, 7}};

    astra::Decision decision;
    decision.role_commands.emplace(10020, attack);
    decision.prompt = "分析中文战报";
    const auto encoded = astra::encode_response(decision);

    astra::test::require(encoded["roleCommandMap"].contains("10020"),
                         "role ID must be encoded as an object key string");
    astra::test::require(encoded["roleCommandMap"]["10020"]["controllerId"] == "10010",
                         "attack controller ID must be preserved");
    astra::test::require(encoded["roleCommandMap"]["10020"]["targetPos"][0] ==
                             nlohmann::json{{"x", 29}, {"y", 7}},
                         "attack target must be preserved");
    astra::test::require(!encoded.contains("executeCmd"),
                         "unused executeCmd must be omitted");
    astra::test::require(encoded.dump().find("分析中文战报") != std::string::npos,
                         "UTF-8 response text must not be escaped or corrupted");
}
