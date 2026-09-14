#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace astra {

struct Pos {
    int x = 0;
    int y = 0;
};

enum class RoleType {
    station,
    gatling,
    railgun,
    rocket,
    wall,
    pioneer,
    worker,
    unknown,
};

struct UnitObservation {
    int id = 0;
    Pos pos;
    RoleType role_type = RoleType::unknown;
    std::string role_type_raw;
    std::optional<int> health;
    std::optional<int> attack_power;
    std::optional<int> attack_range;
    std::optional<int> backpack_capacity;
    std::vector<std::string> backpack;
    std::optional<int> level;
    std::optional<int> cooldown;
    bool owned = false;

    bool controllable() const;
};

struct ZoneObservation {
    Pos pos;
    std::string neutral_type;
};

struct MapObservation {
    int width = 0;
    int height = 0;
    std::vector<ZoneObservation> zones;
};

struct TaskPointObservation {
    std::string task_type;
    Pos position;
    int cooldown_rounds = 0;
    int score_reward = 0;
    int gold_reward = 0;
    bool valid = false;
    std::optional<int> timeout_rounds;
};

struct TeamOurObservation {
    std::string type;
    std::string team_id;
    std::string team_name;
    int gold = 0;
    int total_score = 0;
    std::vector<TaskPointObservation> player_tasks;
    std::vector<UnitObservation> roles;
};

struct RobotObservation {
    int id = 0;
    Pos pos;
    std::string role_type;
    int health = 0;
    std::string abnormal_state;
    std::optional<std::string> target_team;
};

struct WorldNewsObservation {
    std::string official_news;
    std::string folk_legends;
};

struct ShopItemObservation {
    std::string name;
    int price = 0;
};

struct ErrorObservation {
    int code = 0;
    std::string description;
};

struct TurnObservation {
    int round_no = 0;
    MapObservation map;
    TeamOurObservation team_our;
    std::vector<UnitObservation> team_enemy;
    std::vector<RobotObservation> robots;
    std::string phase_task;
    std::map<int, bool> last_round_role_action_results;
    int last_summon_treasure_result = 0;
    std::string llm_response;
    WorldNewsObservation world_news;
    std::string last_command_result;
    std::vector<ShopItemObservation> vendor_shop;
    std::vector<ShopItemObservation> weapon_shop;
    std::vector<ErrorObservation> errors;
    nlohmann::json raw;
};

struct ParseResult {
    std::optional<TurnObservation> turn;
    std::vector<std::string> errors;
};

struct RoleCommand {
    std::string action;
    std::optional<std::string> controller_id;
    std::vector<Pos> target_positions;
    std::optional<std::string> name;
    std::optional<int> number;
    std::optional<std::string> task_answer;
    std::vector<std::string> items;
};

struct Decision {
    std::map<int, RoleCommand> role_commands;
    std::optional<std::string> prompt;
    std::optional<std::string> execute_command;
};

ParseResult parse_turn(const nlohmann::json& input);
nlohmann::json encode_response(const Decision& decision);

}  // namespace astra
