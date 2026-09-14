#include "protocol.hpp"

#include <utility>

namespace astra {
namespace {

template <typename T>
std::optional<T> required(const nlohmann::json& object,
                          const char* key,
                          const std::string& path,
                          std::vector<std::string>& errors) {
    const auto found = object.find(key);
    if (found == object.end()) {
        errors.push_back(path + "." + key + " is required");
        return std::nullopt;
    }
    try {
        return found->get<T>();
    } catch (const nlohmann::json::exception&) {
        errors.push_back(path + "." + key + " has an invalid type");
        return std::nullopt;
    }
}

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object,
                                const char* key,
                                const std::string& path,
                                std::vector<std::string>& errors) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        return std::nullopt;
    }
    try {
        return found->get<T>();
    } catch (const nlohmann::json::exception&) {
        errors.push_back(path + "." + key + " has an invalid type");
        return std::nullopt;
    }
}

std::optional<Pos> parse_pos(const nlohmann::json& value,
                             const std::string& path,
                             std::vector<std::string>& errors) {
    if (!value.is_object()) {
        errors.push_back(path + " must be an object");
        return std::nullopt;
    }
    const auto x = required<int>(value, "x", path, errors);
    const auto y = required<int>(value, "y", path, errors);
    if (!x || !y) {
        return std::nullopt;
    }
    return Pos{*x, *y};
}

std::optional<Pos> required_pos(const nlohmann::json& object,
                                const char* key,
                                const std::string& path,
                                std::vector<std::string>& errors) {
    const auto found = object.find(key);
    if (found == object.end()) {
        errors.push_back(path + "." + key + " is required");
        return std::nullopt;
    }
    return parse_pos(*found, path + "." + key, errors);
}

RoleType parse_role_type(const std::string& value) {
    if (value == "station") return RoleType::station;
    if (value == "gatling") return RoleType::gatling;
    if (value == "railgun") return RoleType::railgun;
    if (value == "rocket") return RoleType::rocket;
    if (value == "wall") return RoleType::wall;
    if (value == "pioneer") return RoleType::pioneer;
    if (value == "worker") return RoleType::worker;
    return RoleType::unknown;
}

std::optional<UnitObservation> parse_unit(const nlohmann::json& value,
                                          const std::string& path,
                                          bool owned,
                                          std::vector<std::string>& errors) {
    if (!value.is_object()) {
        errors.push_back(path + " must be an object");
        return std::nullopt;
    }
    const auto id = required<int>(value, "id", path, errors);
    const auto pos = required_pos(value, "pos", path, errors);
    const auto role_type = required<std::string>(value, "roleType", path, errors);
    if (!id || !pos || !role_type) {
        return std::nullopt;
    }

    UnitObservation unit;
    unit.id = *id;
    unit.pos = *pos;
    unit.role_type_raw = *role_type;
    unit.role_type = parse_role_type(*role_type);
    unit.health = optional_value<int>(value, "health", path, errors);
    unit.attack_power = optional_value<int>(value, "attackPower", path, errors);
    unit.attack_range = optional_value<int>(value, "attackRange", path, errors);
    unit.backpack_capacity = optional_value<int>(value, "backPackCapability", path, errors);
    unit.level = optional_value<int>(value, "level", path, errors);
    unit.cooldown = optional_value<int>(value, "cooldown", path, errors);
    unit.owned = owned;

    const auto backpack = value.find("backpack");
    if (backpack != value.end()) {
        try {
            unit.backpack = backpack->get<std::vector<std::string>>();
        } catch (const nlohmann::json::exception&) {
            errors.push_back(path + ".backpack has an invalid type");
        }
    }
    return unit;
}

std::optional<MapObservation> parse_map(const nlohmann::json& value,
                                        std::vector<std::string>& errors) {
    if (!value.is_object()) {
        errors.push_back("$.mapInfo must be an object");
        return std::nullopt;
    }
    const auto width = required<int>(value, "width", "$.mapInfo", errors);
    const auto height = required<int>(value, "height", "$.mapInfo", errors);
    const auto zones = required<nlohmann::json>(value, "zones", "$.mapInfo", errors);
    if (!width || !height || !zones || !zones->is_array()) {
        if (zones && !zones->is_array()) errors.push_back("$.mapInfo.zones must be an array");
        return std::nullopt;
    }

    MapObservation map;
    map.width = *width;
    map.height = *height;
    for (std::size_t index = 0; index < zones->size(); ++index) {
        const auto& zone_json = (*zones)[index];
        const std::string path = "$.mapInfo.zones[" + std::to_string(index) + "]";
        if (!zone_json.is_object()) {
            errors.push_back(path + " must be an object");
            continue;
        }
        const auto pos = required_pos(zone_json, "pos", path, errors);
        const auto type = required<std::string>(zone_json, "neutralType", path, errors);
        if (pos && type) map.zones.push_back({*pos, *type});
    }
    return map;
}

std::optional<TaskPointObservation> parse_task(const nlohmann::json& value,
                                               const std::string& path,
                                               std::vector<std::string>& errors) {
    if (!value.is_object()) {
        errors.push_back(path + " must be an object");
        return std::nullopt;
    }
    const auto task_type = required<std::string>(value, "taskType", path, errors);
    const auto position = required_pos(value, "taskPosition", path, errors);
    const auto cooldown = required<int>(value, "coldDownRounds", path, errors);
    const auto score = required<int>(value, "scoreReward", path, errors);
    const auto gold = required<int>(value, "goldReward", path, errors);
    const auto valid = required<bool>(value, "isValid", path, errors);
    if (!task_type || !position || !cooldown || !score || !gold || !valid) {
        return std::nullopt;
    }
    return TaskPointObservation{*task_type,
                                *position,
                                *cooldown,
                                *score,
                                *gold,
                                *valid,
                                optional_value<int>(value, "timeoutRounds", path, errors)};
}

std::optional<TeamOurObservation> parse_our_team(const nlohmann::json& value,
                                                 std::vector<std::string>& errors) {
    if (!value.is_object()) {
        errors.push_back("$.teamOur must be an object");
        return std::nullopt;
    }
    const auto type = required<std::string>(value, "type", "$.teamOur", errors);
    const auto team_id = required<std::string>(value, "teamId", "$.teamOur", errors);
    const auto team_name = required<std::string>(value, "teamName", "$.teamOur", errors);
    const auto gold = required<int>(value, "goldNum", "$.teamOur", errors);
    const auto score = required<int>(value, "totalScore", "$.teamOur", errors);
    const auto tasks = required<nlohmann::json>(value, "playerTasks", "$.teamOur", errors);
    const auto roles = required<nlohmann::json>(value, "roles", "$.teamOur", errors);
    if (!type || !team_id || !team_name || !gold || !score || !tasks || !roles ||
        !tasks->is_array() || !roles->is_array()) {
        if (tasks && !tasks->is_array()) errors.push_back("$.teamOur.playerTasks must be an array");
        if (roles && !roles->is_array()) errors.push_back("$.teamOur.roles must be an array");
        return std::nullopt;
    }

    TeamOurObservation team{*type, *team_id, *team_name, *gold, *score, {}, {}};
    for (std::size_t index = 0; index < tasks->size(); ++index) {
        auto task = parse_task((*tasks)[index],
                               "$.teamOur.playerTasks[" + std::to_string(index) + "]",
                               errors);
        if (task) team.player_tasks.push_back(std::move(*task));
    }
    for (std::size_t index = 0; index < roles->size(); ++index) {
        auto unit = parse_unit((*roles)[index],
                               "$.teamOur.roles[" + std::to_string(index) + "]",
                               true,
                               errors);
        if (unit) team.roles.push_back(std::move(*unit));
    }
    return team;
}

std::vector<UnitObservation> parse_enemy(const nlohmann::json& value,
                                         std::vector<std::string>& errors) {
    std::vector<UnitObservation> result;
    if (!value.is_object() || !value.contains("roles") || !value["roles"].is_array()) {
        errors.push_back("$.teamEnemy.roles must be an array");
        return result;
    }
    const auto& roles = value["roles"];
    for (std::size_t index = 0; index < roles.size(); ++index) {
        auto unit = parse_unit(roles[index],
                               "$.teamEnemy.roles[" + std::to_string(index) + "]",
                               false,
                               errors);
        if (unit) result.push_back(std::move(*unit));
    }
    return result;
}

std::vector<RobotObservation> parse_robots(const nlohmann::json& value,
                                           std::vector<std::string>& errors) {
    std::vector<RobotObservation> result;
    if (!value.is_object() || !value.contains("roles") || !value["roles"].is_array()) {
        errors.push_back("$.robot.roles must be an array");
        return result;
    }
    const auto& roles = value["roles"];
    for (std::size_t index = 0; index < roles.size(); ++index) {
        const auto& robot = roles[index];
        const std::string path = "$.robot.roles[" + std::to_string(index) + "]";
        if (!robot.is_object()) {
            errors.push_back(path + " must be an object");
            continue;
        }
        const auto id = required<int>(robot, "id", path, errors);
        const auto pos = required_pos(robot, "pos", path, errors);
        const auto type = required<std::string>(robot, "roleType", path, errors);
        const auto health = required<int>(robot, "health", path, errors);
        const auto state = required<std::string>(robot, "abnormalState", path, errors);
        if (id && pos && type && health && state) {
            result.push_back({*id,
                              *pos,
                              *type,
                              *health,
                              *state,
                              optional_value<std::string>(robot, "targetTeam", path, errors)});
        }
    }
    return result;
}

std::vector<ShopItemObservation> parse_shop(const nlohmann::json& value,
                                            const std::string& path,
                                            std::vector<std::string>& errors) {
    std::vector<ShopItemObservation> result;
    if (!value.is_array()) {
        errors.push_back(path + " must be an array");
        return result;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto& item = value[index];
        const std::string item_path = path + "[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            errors.push_back(item_path + " must be an object");
            continue;
        }
        const auto name = required<std::string>(item, "name", item_path, errors);
        const auto price = required<int>(item, "price", item_path, errors);
        if (name && price) result.push_back({*name, *price});
    }
    return result;
}

}  // namespace

bool UnitObservation::controllable() const {
    return owned && role_type != RoleType::unknown;
}

ParseResult parse_turn(const nlohmann::json& input) {
    ParseResult result;
    if (!input.is_object()) {
        result.errors.push_back("$ must be an object");
        return result;
    }

    const auto round_no = required<int>(input, "roundNo", "$", result.errors);
    const auto map_json = required<nlohmann::json>(input, "mapInfo", "$", result.errors);
    const auto our_json = required<nlohmann::json>(input, "teamOur", "$", result.errors);
    if (!round_no || !map_json || !our_json) return result;

    auto map = parse_map(*map_json, result.errors);
    auto team = parse_our_team(*our_json, result.errors);
    if (!map || !team) return result;

    TurnObservation turn;
    turn.round_no = *round_no;
    turn.map = std::move(*map);
    turn.team_our = std::move(*team);
    turn.raw = input;

    const auto enemy = input.find("teamEnemy");
    const auto robots = input.find("robot");
    if (enemy == input.end()) {
        result.errors.push_back("$.teamEnemy is required");
    } else {
        turn.team_enemy = parse_enemy(*enemy, result.errors);
    }
    if (robots == input.end()) {
        result.errors.push_back("$.robot is required");
    } else {
        turn.robots = parse_robots(*robots, result.errors);
    }

    turn.phase_task = required<std::string>(input, "phaseTask", "$", result.errors).value_or("");
    turn.last_summon_treasure_result =
        required<int>(input, "lastSummonTreasureResult", "$", result.errors).value_or(0);
    turn.llm_response = required<std::string>(input, "llmResp", "$", result.errors).value_or("");
    turn.last_command_result =
        required<std::string>(input, "lastCmdResult", "$", result.errors).value_or("");

    const auto action_results = input.find("lastRoundRoleActionResults");
    if (action_results == input.end() || !action_results->is_object()) {
        result.errors.push_back("$.lastRoundRoleActionResults must be an object");
    } else {
        for (auto item = action_results->begin(); item != action_results->end(); ++item) {
            try {
                turn.last_round_role_action_results.emplace(std::stoi(item.key()),
                                                              item.value().get<bool>());
            } catch (const std::exception&) {
                result.errors.push_back("$.lastRoundRoleActionResults." + item.key() +
                                        " is invalid");
            }
        }
    }

    const auto news = input.find("worldNews");
    if (news == input.end() || !news->is_object()) {
        result.errors.push_back("$.worldNews must be an object");
    } else {
        turn.world_news.official_news =
            required<std::string>(*news, "officialNews", "$.worldNews", result.errors)
                .value_or("");
        turn.world_news.folk_legends =
            required<std::string>(*news, "folkLegends", "$.worldNews", result.errors)
                .value_or("");
    }

    const auto vendor = input.find("vendorShopList");
    const auto weapons = input.find("weaponShopList");
    if (vendor == input.end()) {
        result.errors.push_back("$.vendorShopList is required");
    } else {
        turn.vendor_shop = parse_shop(*vendor, "$.vendorShopList", result.errors);
    }
    if (weapons == input.end()) {
        result.errors.push_back("$.weaponShopList is required");
    } else {
        turn.weapon_shop = parse_shop(*weapons, "$.weaponShopList", result.errors);
    }

    const auto errors = input.find("errors");
    if (errors == input.end() || !errors->is_array()) {
        result.errors.push_back("$.errors must be an array");
    } else {
        for (std::size_t index = 0; index < errors->size(); ++index) {
            const auto& error = (*errors)[index];
            const std::string path = "$.errors[" + std::to_string(index) + "]";
            if (!error.is_object()) {
                result.errors.push_back(path + " must be an object");
                continue;
            }
            const auto code = required<int>(error, "errorCode", path, result.errors);
            const auto description =
                required<std::string>(error, "description", path, result.errors);
            if (code && description) turn.errors.push_back({*code, *description});
        }
    }

    result.turn = std::move(turn);
    return result;
}

nlohmann::json encode_response(const Decision& decision) {
    nlohmann::json role_commands = nlohmann::json::object();
    for (const auto& [role_id, command] : decision.role_commands) {
        nlohmann::json encoded{{"action", command.action}};
        if (command.controller_id) encoded["controllerId"] = *command.controller_id;
        if (!command.target_positions.empty()) {
            encoded["targetPos"] = nlohmann::json::array();
            for (const auto& pos : command.target_positions) {
                encoded["targetPos"].push_back({{"x", pos.x}, {"y", pos.y}});
            }
        }
        if (command.name) encoded["name"] = *command.name;
        if (command.number) encoded["num"] = *command.number;
        if (command.task_answer) encoded["taskAnswer"] = *command.task_answer;
        if (!command.items.empty()) encoded["item"] = command.items;
        role_commands[std::to_string(role_id)] = std::move(encoded);
    }

    nlohmann::json response{{"roleCommandMap", std::move(role_commands)}};
    if (decision.prompt) response["prompt"] = *decision.prompt;
    if (decision.execute_command) response["executeCmd"] = *decision.execute_command;
    return response;
}

}  // namespace astra
