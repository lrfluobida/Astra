#pragma once

#include <map>
#include "optional.hpp"
#include <string>
#include <vector>

#include <json/json.h>

namespace astra {

struct Pos {
    constexpr Pos(int x_value = 0, int y_value = 0) : x(x_value), y(y_value) {}
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
    astra::Optional<int> health;
    astra::Optional<int> attack_power;
    astra::Optional<int> attack_range;
    astra::Optional<int> backpack_capacity;
    std::vector<std::string> backpack;
    astra::Optional<int> level;
    astra::Optional<int> cooldown;
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
    TaskPointObservation() = default;
    TaskPointObservation(std::string type, Pos pos, int cooldown, int score, int gold,
                         bool is_valid, astra::Optional<int> timeout = astra::nullopt)
        : task_type(std::move(type)), position(pos), cooldown_rounds(cooldown),
          score_reward(score), gold_reward(gold), valid(is_valid), timeout_rounds(timeout) {}
    std::string task_type;
    Pos position;
    int cooldown_rounds = 0;
    int score_reward = 0;
    int gold_reward = 0;
    bool valid = false;
    astra::Optional<int> timeout_rounds;
};

struct TeamOurObservation {
    TeamOurObservation() = default;
    TeamOurObservation(std::string side, std::string id, std::string name, int money, int score,
                       std::vector<TaskPointObservation> tasks, std::vector<UnitObservation> units)
        : type(std::move(side)), team_id(std::move(id)), team_name(std::move(name)),
          gold(money), total_score(score), player_tasks(std::move(tasks)), roles(std::move(units)) {}
    std::string type;
    std::string team_id;
    std::string team_name;
    int gold = 0;
    int total_score = 0;
    std::vector<TaskPointObservation> player_tasks;
    std::vector<UnitObservation> roles;
};

struct RobotObservation {
    RobotObservation() = default;
    RobotObservation(int robot_id, Pos position, std::string type, int hp,
                     std::string state, astra::Optional<std::string> target = astra::nullopt)
        : id(robot_id), pos(position), role_type(std::move(type)), health(hp),
          abnormal_state(std::move(state)), target_team(std::move(target)) {}
    int id = 0;
    Pos pos;
    std::string role_type;
    int health = 0;
    std::string abnormal_state;
    astra::Optional<std::string> target_team;
};

struct WorldNewsObservation {
    std::string official_news;
    std::string folk_legends;
};

struct ShopItemObservation {
    ShopItemObservation() = default;
    ShopItemObservation(std::string item_name, int item_price)
        : name(std::move(item_name)), price(item_price) {}
    std::string name;
    int price = 0;
};

struct ErrorObservation {
    ErrorObservation() = default;
    ErrorObservation(int error_code, std::string message)
        : code(error_code), description(std::move(message)) {}
    int code = 0;
    std::string description;
};

struct TurnObservation {
    int round_no = 0;
    int summon_orders_used = 0;
    MapObservation map;
    TeamOurObservation team_our;
    std::vector<UnitObservation> team_enemy;
    std::vector<RobotObservation> robots;
    std::string phase_task;
    std::string task_history;
    std::map<int, bool> last_round_role_action_results;
    int last_summon_treasure_result = 0;
    std::string llm_response;
    WorldNewsObservation world_news;
    std::string last_command_result;
    std::vector<ShopItemObservation> vendor_shop;
    std::vector<ShopItemObservation> weapon_shop;
    std::vector<ErrorObservation> errors;
    Json::Value raw;
};

struct ParseResult {
    astra::Optional<TurnObservation> turn;
    std::vector<std::string> errors;
};

struct RoleCommand {
    std::string action;
    astra::Optional<std::string> controller_id;
    std::vector<Pos> target_positions;
    astra::Optional<std::string> name;
    astra::Optional<int> number;
    astra::Optional<std::string> task_answer;
    std::vector<std::string> items;
};

struct Decision {
    std::map<int, RoleCommand> role_commands;
    astra::Optional<std::string> prompt;
    astra::Optional<std::string> execute_command;
};

ParseResult parse_turn(const Json::Value& input);
Json::Value encode_response(const Decision& decision);
bool is_robot_summon_order(const std::string& name);

}  // namespace astra
