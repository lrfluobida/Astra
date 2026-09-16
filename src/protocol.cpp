#include "protocol.hpp"

#include <utility>

namespace astra {
namespace {

template <typename T>
struct JsonConversion;

template <>
struct JsonConversion<int> {
    static bool valid(const Json::Value& value) { return value.isInt(); }
    static int get(const Json::Value& value) { return value.asInt(); }
};

template <>
struct JsonConversion<bool> {
    static bool valid(const Json::Value& value) { return value.isBool(); }
    static bool get(const Json::Value& value) { return value.asBool(); }
};

template <>
struct JsonConversion<std::string> {
    static bool valid(const Json::Value& value) { return value.isString(); }
    static std::string get(const Json::Value& value) { return value.asString(); }
};

template <>
struct JsonConversion<Json::Value> {
    static bool valid(const Json::Value&) { return true; }
    static Json::Value get(const Json::Value& value) { return value; }
};

template <typename T>
std::optional<T> required(const Json::Value& object,
                          const char* key,
                          const std::string& path,
                          std::vector<std::string>& errors) {
    if (!object.isObject() || !object.isMember(key)) {
        errors.push_back(path + "." + key + " is required");
        return std::nullopt;
    }
    const Json::Value& value = object[key];
    if (!JsonConversion<T>::valid(value)) {
        errors.push_back(path + "." + key + " has an invalid type");
        return std::nullopt;
    }
    return JsonConversion<T>::get(value);
}

template <typename T>
std::optional<T> optional_value(const Json::Value& object,
                                const char* key,
                                const std::string& path,
                                std::vector<std::string>& errors) {
    if (!object.isObject() || !object.isMember(key) || object[key].isNull()) {
        return std::nullopt;
    }
    const Json::Value& value = object[key];
    if (!JsonConversion<T>::valid(value)) {
        errors.push_back(path + "." + key + " has an invalid type");
        return std::nullopt;
    }
    return JsonConversion<T>::get(value);
}

std::optional<Pos> parse_pos(const Json::Value& value,
                             const std::string& path,
                             std::vector<std::string>& errors) {
    if (!value.isObject()) {
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

std::optional<Pos> required_pos(const Json::Value& object,
                                const char* key,
                                const std::string& path,
                                std::vector<std::string>& errors) {
    if (!object.isObject() || !object.isMember(key)) {
        errors.push_back(path + "." + key + " is required");
        return std::nullopt;
    }
    return parse_pos(object[key], path + "." + key, errors);
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

std::optional<UnitObservation> parse_unit(const Json::Value& value,
                                          const std::string& path,
                                          bool owned,
                                          std::vector<std::string>& errors) {
    if (!value.isObject()) {
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

    if (value.isMember("backpack") && !value["backpack"].isNull()) {
        const Json::Value& backpack = value["backpack"];
        if (!backpack.isArray()) {
            errors.push_back(path + ".backpack has an invalid type");
        } else {
            bool valid = true;
            for (Json::ArrayIndex index = 0; index < backpack.size(); ++index) {
                if (!backpack[index].isString()) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                errors.push_back(path + ".backpack has an invalid type");
            } else {
                for (Json::ArrayIndex index = 0; index < backpack.size(); ++index) {
                    unit.backpack.push_back(backpack[index].asString());
                }
            }
        }
    }
    return unit;
}

std::optional<MapObservation> parse_map(const Json::Value& value,
                                        std::vector<std::string>& errors) {
    if (!value.isObject()) {
        errors.push_back("$.mapInfo must be an object");
        return std::nullopt;
    }
    const auto width = required<int>(value, "width", "$.mapInfo", errors);
    const auto height = required<int>(value, "height", "$.mapInfo", errors);
    const auto zones = required<Json::Value>(value, "zones", "$.mapInfo", errors);
    if (!width || !height || !zones || !zones->isArray()) {
        if (zones && !zones->isArray()) errors.push_back("$.mapInfo.zones must be an array");
        return std::nullopt;
    }

    MapObservation map;
    map.width = *width;
    map.height = *height;
    for (std::size_t index = 0; index < zones->size(); ++index) {
        const auto& zone_json = (*zones)[static_cast<Json::ArrayIndex>(index)];
        const std::string path = "$.mapInfo.zones[" + std::to_string(index) + "]";
        if (!zone_json.isObject()) {
            errors.push_back(path + " must be an object");
            continue;
        }
        const auto pos = required_pos(zone_json, "pos", path, errors);
        const auto type = required<std::string>(zone_json, "neutralType", path, errors);
        if (pos && type) map.zones.push_back({*pos, *type});
    }
    return map;
}

std::optional<TaskPointObservation> parse_task(const Json::Value& value,
                                               const std::string& path,
                                               std::vector<std::string>& errors) {
    if (!value.isObject()) {
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

std::optional<TeamOurObservation> parse_our_team(const Json::Value& value,
                                                 std::vector<std::string>& errors) {
    if (!value.isObject()) {
        errors.push_back("$.teamOur must be an object");
        return std::nullopt;
    }
    const auto type = required<std::string>(value, "type", "$.teamOur", errors);
    const auto team_id = required<std::string>(value, "teamId", "$.teamOur", errors);
    const auto team_name = required<std::string>(value, "teamName", "$.teamOur", errors);
    const auto gold = required<int>(value, "goldNum", "$.teamOur", errors);
    const auto score = required<int>(value, "totalScore", "$.teamOur", errors);
    const auto tasks = required<Json::Value>(value, "playerTasks", "$.teamOur", errors);
    const auto roles = required<Json::Value>(value, "roles", "$.teamOur", errors);
    if (!type || !team_id || !team_name || !gold || !score || !tasks || !roles ||
        !tasks->isArray() || !roles->isArray()) {
        if (tasks && !tasks->isArray()) errors.push_back("$.teamOur.playerTasks must be an array");
        if (roles && !roles->isArray()) errors.push_back("$.teamOur.roles must be an array");
        return std::nullopt;
    }

    TeamOurObservation team{*type, *team_id, *team_name, *gold, *score, {}, {}};
    for (std::size_t index = 0; index < tasks->size(); ++index) {
        auto task = parse_task((*tasks)[static_cast<Json::ArrayIndex>(index)],
                               "$.teamOur.playerTasks[" + std::to_string(index) + "]",
                               errors);
        if (task) team.player_tasks.push_back(std::move(*task));
    }
    for (std::size_t index = 0; index < roles->size(); ++index) {
        auto unit = parse_unit((*roles)[static_cast<Json::ArrayIndex>(index)],
                               "$.teamOur.roles[" + std::to_string(index) + "]",
                               true,
                               errors);
        if (unit) team.roles.push_back(std::move(*unit));
    }
    return team;
}

std::vector<UnitObservation> parse_enemy(const Json::Value& value,
                                         std::vector<std::string>& errors) {
    std::vector<UnitObservation> result;
    if (!value.isObject() || !value.isMember("roles") || !value["roles"].isArray()) {
        errors.push_back("$.teamEnemy.roles must be an array");
        return result;
    }
    const auto& roles = value["roles"];
    for (std::size_t index = 0; index < roles.size(); ++index) {
        auto unit = parse_unit(roles[static_cast<Json::ArrayIndex>(index)],
                               "$.teamEnemy.roles[" + std::to_string(index) + "]",
                               false,
                               errors);
        if (unit) result.push_back(std::move(*unit));
    }
    return result;
}

std::vector<RobotObservation> parse_robots(const Json::Value& value,
                                           std::vector<std::string>& errors) {
    std::vector<RobotObservation> result;
    if (!value.isObject() || !value.isMember("roles") || !value["roles"].isArray()) {
        errors.push_back("$.robot.roles must be an array");
        return result;
    }
    const auto& roles = value["roles"];
    for (std::size_t index = 0; index < roles.size(); ++index) {
        const auto& robot = roles[static_cast<Json::ArrayIndex>(index)];
        const std::string path = "$.robot.roles[" + std::to_string(index) + "]";
        if (!robot.isObject()) {
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

std::vector<ShopItemObservation> parse_shop(const Json::Value& value,
                                            const std::string& path,
                                            std::vector<std::string>& errors) {
    std::vector<ShopItemObservation> result;
    if (!value.isArray()) {
        errors.push_back(path + " must be an array");
        return result;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto& item = value[static_cast<Json::ArrayIndex>(index)];
        const std::string item_path = path + "[" + std::to_string(index) + "]";
        if (!item.isObject()) {
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

bool is_robot_summon_order(const std::string& name) {
    return name == "SmallRobotSummonOrder" || name == "MiddleRobotSummonOrder" ||
           name == "LargeRobotSummonOrder" || name == "BossRobotSummonOrder";
}

ParseResult parse_turn(const Json::Value& input) {
    ParseResult result;
    if (!input.isObject()) {
        result.errors.push_back("$ must be an object");
        return result;
    }

    const auto round_no = required<int>(input, "roundNo", "$", result.errors);
    const auto map_json = required<Json::Value>(input, "mapInfo", "$", result.errors);
    const auto our_json = required<Json::Value>(input, "teamOur", "$", result.errors);
    if (!round_no || !map_json || !our_json) return result;

    auto map = parse_map(*map_json, result.errors);
    auto team = parse_our_team(*our_json, result.errors);
    if (!map || !team) return result;

    TurnObservation turn;
    turn.round_no = *round_no;
    turn.map = std::move(*map);
    turn.team_our = std::move(*team);
    turn.raw = input;

    if (!input.isMember("teamEnemy")) {
        result.errors.push_back("$.teamEnemy is required");
    } else {
        turn.team_enemy = parse_enemy(input["teamEnemy"], result.errors);
    }
    if (!input.isMember("robot")) {
        result.errors.push_back("$.robot is required");
    } else {
        turn.robots = parse_robots(input["robot"], result.errors);
    }

    turn.phase_task = required<std::string>(input, "phaseTask", "$", result.errors).value_or("");
    turn.last_summon_treasure_result =
        required<int>(input, "lastSummonTreasureResult", "$", result.errors).value_or(0);
    turn.llm_response = required<std::string>(input, "llmResp", "$", result.errors).value_or("");
    turn.last_command_result =
        required<std::string>(input, "lastCmdResult", "$", result.errors).value_or("");

    if (!input.isMember("lastRoundRoleActionResults") ||
        !input["lastRoundRoleActionResults"].isObject()) {
        result.errors.push_back("$.lastRoundRoleActionResults must be an object");
    } else {
        const Json::Value& action_results = input["lastRoundRoleActionResults"];
        for (const auto& key : action_results.getMemberNames()) {
            try {
                if (!action_results[key].isBool()) throw std::invalid_argument("not bool");
                turn.last_round_role_action_results.emplace(std::stoi(key),
                                                              action_results[key].asBool());
            } catch (const std::exception&) {
                result.errors.push_back("$.lastRoundRoleActionResults." + key +
                                        " is invalid");
            }
        }
    }

    if (!input.isMember("worldNews") || !input["worldNews"].isObject()) {
        result.errors.push_back("$.worldNews must be an object");
    } else {
        const Json::Value& news = input["worldNews"];
        turn.world_news.official_news =
            required<std::string>(news, "officialNews", "$.worldNews", result.errors)
                .value_or("");
        turn.world_news.folk_legends =
            required<std::string>(news, "folkLegends", "$.worldNews", result.errors)
                .value_or("");
    }

    if (!input.isMember("vendorShopList")) {
        result.errors.push_back("$.vendorShopList is required");
    } else {
        turn.vendor_shop = parse_shop(input["vendorShopList"], "$.vendorShopList", result.errors);
    }
    if (!input.isMember("weaponShopList")) {
        result.errors.push_back("$.weaponShopList is required");
    } else {
        turn.weapon_shop = parse_shop(input["weaponShopList"], "$.weaponShopList", result.errors);
    }

    if (!input.isMember("errors") || !input["errors"].isArray()) {
        result.errors.push_back("$.errors must be an array");
    } else {
        const Json::Value& errors = input["errors"];
        for (std::size_t index = 0; index < errors.size(); ++index) {
            const auto& error = errors[static_cast<Json::ArrayIndex>(index)];
            const std::string path = "$.errors[" + std::to_string(index) + "]";
            if (!error.isObject()) {
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

Json::Value encode_response(const Decision& decision) {
    Json::Value role_commands(Json::objectValue);
    for (const auto& [role_id, command] : decision.role_commands) {
        Json::Value encoded(Json::objectValue);
        encoded["action"] = command.action;
        if (command.controller_id) encoded["controllerId"] = *command.controller_id;
        if (!command.target_positions.empty()) {
            encoded["targetPos"] = Json::Value(Json::arrayValue);
            for (const auto& pos : command.target_positions) {
                Json::Value target(Json::objectValue);
                target["x"] = pos.x;
                target["y"] = pos.y;
                encoded["targetPos"].append(std::move(target));
            }
        }
        if (command.name) encoded["name"] = *command.name;
        if (command.number) encoded["num"] = *command.number;
        if (command.task_answer) encoded["taskAnswer"] = *command.task_answer;
        if (!command.items.empty()) {
            Json::Value items(Json::arrayValue);
            for (const auto& item : command.items) items.append(item);
            encoded["item"] = std::move(items);
        }
        role_commands[std::to_string(role_id)] = std::move(encoded);
    }

    Json::Value response(Json::objectValue);
    response["roleCommandMap"] = std::move(role_commands);
    if (decision.prompt) response["prompt"] = *decision.prompt;
    if (decision.execute_command) response["executeCmd"] = *decision.execute_command;
    return response;
}

}  // namespace astra
