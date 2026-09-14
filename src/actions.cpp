#include "actions.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace astra {
namespace {

bool same_pos(const Pos& left, const Pos& right) {
    return left.x == right.x && left.y == right.y;
}

int distance(const Pos& left, const Pos& right) {
    return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
}

bool is_day(int round_no) {
    const int round_in_day = (round_no - 1) % 130 + 1;
    return round_in_day <= 70;
}

const UnitObservation* find_own_unit(const TurnObservation& turn, int id) {
    for (const auto& unit : turn.team_our.roles) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

bool alive(const UnitObservation* unit) {
    return unit && unit->owned && unit->health && *unit->health > 0 && unit->controllable();
}

bool is_character(const UnitObservation& unit) {
    return unit.role_type == RoleType::worker || unit.role_type == RoleType::pioneer;
}

bool is_weapon(const UnitObservation& unit) {
    return unit.role_type == RoleType::gatling || unit.role_type == RoleType::railgun ||
           unit.role_type == RoleType::rocket;
}

bool contains_pos(const std::vector<Pos>& positions, const Pos& target) {
    return std::any_of(positions.begin(), positions.end(), [&](const Pos& pos) {
        return same_pos(pos, target);
    });
}

bool in_bounds(const TurnObservation& turn, const Pos& pos) {
    return pos.x >= 0 && pos.y >= 0 && pos.x < turn.map.width && pos.y < turn.map.height;
}

bool building_covers(const UnitObservation& unit, const Pos& pos) {
    if (unit.role_type == RoleType::station) {
        return pos.x >= unit.pos.x && pos.x <= unit.pos.x + 1 && pos.y >= unit.pos.y &&
               pos.y <= unit.pos.y + 1;
    }
    return !is_character(unit) && same_pos(unit.pos, pos);
}

bool move_destination_blocked(const TurnObservation& turn, const Pos& pos) {
    for (const auto& zone : turn.map.zones) {
        if (same_pos(zone.pos, pos)) return true;
    }
    for (const auto& unit : turn.team_our.roles) {
        if (is_character(unit) ? same_pos(unit.pos, pos) : building_covers(unit, pos)) return true;
    }
    for (const auto& unit : turn.team_enemy) {
        if (is_character(unit) ? same_pos(unit.pos, pos) : building_covers(unit, pos)) return true;
    }
    for (const auto& robot : turn.robots) {
        if (same_pos(robot.pos, pos)) return true;
    }
    return false;
}

int item_count(const UnitObservation& unit, const std::string& item) {
    return static_cast<int>(std::count(unit.backpack.begin(), unit.backpack.end(), item));
}

bool parse_id(const std::string& text, int& result) {
    try {
        std::size_t consumed = 0;
        result = std::stoi(text, &consumed);
        return consumed == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::string validate_move(const TurnObservation& turn,
                          const CandidateAction& candidate,
                          Pos& destination) {
    const auto* actor = find_own_unit(turn, candidate.action_key);
    if (!alive(actor) || !is_character(*actor)) return "move actor is not a living character";
    if (candidate.command.target_positions.size() != 1) return "move requires one targetPos";
    destination = candidate.command.target_positions.front();
    if (!in_bounds(turn, destination)) return "move target is outside the map";
    if (distance(actor->pos, destination) != 1) return "move target must be one cell away";
    if (move_destination_blocked(turn, destination)) return "move target is occupied";
    return "";
}

std::string validate_attack(const TurnObservation& turn,
                            const CandidateAction& candidate,
                            int& controller_id) {
    const auto* weapon = find_own_unit(turn, candidate.action_key);
    if (!alive(weapon) || !is_weapon(*weapon)) return "attack key is not a living weapon";
    if (is_day(turn.round_no)) return "attack is unavailable during daytime";
    if (!candidate.command.controller_id ||
        !parse_id(*candidate.command.controller_id, controller_id)) {
        return "attack requires a valid controllerId";
    }
    const auto* controller = find_own_unit(turn, controller_id);
    if (!alive(controller) || !is_character(*controller)) {
        return "attack controller is not a living character";
    }
    if (distance(controller->pos, weapon->pos) > 1) {
        return "attack controller is not adjacent to the weapon";
    }
    if (!weapon->attack_range || !weapon->level) return "weapon range or level is unknown";
    if (weapon->role_type == RoleType::rocket &&
        (!weapon->cooldown || *weapon->cooldown != 0)) {
        return "rocket cooldown is unknown or active";
    }

    const std::size_t expected =
        weapon->role_type == RoleType::railgun ? 1U : static_cast<std::size_t>(*weapon->level);
    if (candidate.command.target_positions.size() != expected) {
        return "attack target count does not match weapon level";
    }
    for (const auto& target : candidate.command.target_positions) {
        if (!in_bounds(turn, target) || distance(weapon->pos, target) > *weapon->attack_range ||
            distance(weapon->pos, target) == 0) {
            return "attack target is outside the weapon range";
        }
    }

    if (weapon->role_type == RoleType::gatling) {
        for (std::size_t left = 0; left < candidate.command.target_positions.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < candidate.command.target_positions.size();
                 ++right) {
                const auto& a = candidate.command.target_positions[left];
                const auto& b = candidate.command.target_positions[right];
                const long long ax = a.x - weapon->pos.x;
                const long long ay = a.y - weapon->pos.y;
                const long long bx = b.x - weapon->pos.x;
                const long long by = b.y - weapon->pos.y;
                if (ax * bx + ay * by < 0) return "gatling target angle exceeds 90 degrees";
            }
        }
    }
    return "";
}

std::string validate_build(const TurnObservation& turn,
                           const CandidateAction& candidate,
                           const ArbitrationRules& rules) {
    const auto* actor = find_own_unit(turn, candidate.action_key);
    if (!alive(actor) || actor->role_type != RoleType::worker) {
        return "build actor is not a living worker";
    }
    if (!is_day(turn.round_no)) return "build is unavailable at night";
    if (!candidate.command.name || candidate.command.target_positions.size() != 1) {
        return "build requires name and one targetPos";
    }
    const Pos target = candidate.command.target_positions.front();
    if (!in_bounds(turn, target) || distance(actor->pos, target) != 1) {
        return "build target must be an adjacent map cell";
    }
    const std::string& name = *candidate.command.name;
    if (name == "gatling" || name == "railgun" || name == "rocket") {
        if (!contains_pos(rules.weapon_build_tiles, target)) return "weapon build area is unknown";
        if (candidate.reservation.gold != 25) return "weapon build must reserve 25 gold";
        return "";
    }
    if (name == "wall") {
        if (!contains_pos(rules.wall_build_tiles, target)) return "wall build area is unknown";
        const auto owner = candidate.reservation.items.find(candidate.action_key);
        const auto stone = owner == candidate.reservation.items.end()
                               ? std::map<std::string, int>::const_iterator{}
                               : owner->second.find("stone");
        if (owner == candidate.reservation.items.end() || stone == owner->second.end() ||
            stone->second != 1) {
            return "wall build must reserve one stone";
        }
        return "";
    }
    return "unsupported build name";
}

std::string validate_use(const TurnObservation& turn, const CandidateAction& candidate) {
    const auto* actor = find_own_unit(turn, candidate.action_key);
    if (!alive(actor) || !is_character(*actor)) return "use actor is not a living character";
    if (!candidate.command.name) return "use requires name";
    const auto owner = candidate.reservation.items.find(candidate.action_key);
    if (owner == candidate.reservation.items.end()) return "use must reserve its item";
    const auto item = owner->second.find(*candidate.command.name);
    if (item == owner->second.end() || item->second != 1) return "use must reserve one named item";
    return "";
}

std::string validate_submit(const TurnObservation& turn, const CandidateAction& candidate) {
    const auto* actor = find_own_unit(turn, candidate.action_key);
    if (!alive(actor) || actor->role_type != RoleType::pioneer) {
        return "submitAnswer actor is not the living pioneer";
    }
    if (turn.phase_task.empty()) return "submitAnswer requires an active task";
    if (!candidate.command.task_answer) return "submitAnswer requires taskAnswer";
    return "";
}

std::string validate_action(const TurnObservation& turn,
                            const CandidateAction& candidate,
                            const ArbitrationRules& rules,
                            std::optional<int>& controller,
                            std::optional<Pos>& move_destination) {
    if (candidate.command.action == "move") {
        Pos destination;
        const auto error = validate_move(turn, candidate, destination);
        if (error.empty()) move_destination = destination;
        return error;
    }
    if (candidate.command.action == "attack") {
        int controller_id = 0;
        const auto error = validate_attack(turn, candidate, controller_id);
        if (error.empty()) controller = controller_id;
        return error;
    }
    if (candidate.command.action == "build") return validate_build(turn, candidate, rules);
    if (candidate.command.action == "use") return validate_use(turn, candidate);
    if (candidate.command.action == "submitAnswer") return validate_submit(turn, candidate);

    const std::string& action = candidate.command.action;
    if ((action == "sell" || action == "buy") && !candidate.command.name) {
        return action + " requires name";
    }
    if ((action == "sell" || action == "buy") && candidate.command.number &&
        *candidate.command.number <= 0) {
        return action + " num must be positive";
    }
    if ((action == "remove" || action == "collect") &&
        candidate.command.target_positions.size() != 1) {
        return action + " requires one targetPos";
    }
    if (action == "summonTreasure" &&
        (candidate.command.target_positions.size() != 1 || candidate.command.items.empty())) {
        return "summonTreasure requires one targetPos and item";
    }
    if (action == "drop" && !candidate.command.name) return "drop requires name";

    static const std::set<std::string> defined_actions = {
        "sell", "buy", "remove", "acceptTask", "summonTreasure", "drop", "collect"};
    if (defined_actions.count(action) != 0) {
        return "defined action is not supported by this arbitration stage";
    }
    return "unknown action";
}

}  // namespace

int ArbitrationResult::actor_use_count(int actor_id) const {
    const auto found = actor_uses.find(actor_id);
    return found == actor_uses.end() ? 0 : found->second;
}

int ArbitrationResult::gold_reserved() const {
    return reserved_gold;
}

ArbitrationResult arbitrate(const TurnObservation& turn,
                            const std::vector<CandidateAction>& actions,
                            const std::vector<TopLevelCandidate>& top_level,
                            const ArbitrationRules& rules) {
    ArbitrationResult result;
    std::vector<std::size_t> order(actions.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return actions[left].priority > actions[right].priority;
    });

    std::set<int> used_actors;
    std::vector<Pos> move_destinations;
    std::map<int, std::map<std::string, int>> reserved_items;

    for (const std::size_t index : order) {
        const auto& candidate = actions[index];
        std::optional<int> controller;
        std::optional<Pos> move_destination;
        std::string reason =
            validate_action(turn, candidate, rules, controller, move_destination);

        if (reason.empty() && result.reserved_gold + candidate.reservation.gold > turn.team_our.gold) {
            reason = "shared gold reservation exceeds available gold";
        }
        if (reason.empty()) {
            for (const auto& [owner_id, items] : candidate.reservation.items) {
                const auto* owner = find_own_unit(turn, owner_id);
                if (!owner) {
                    reason = "item reservation owner is missing";
                    break;
                }
                for (const auto& [name, count] : items) {
                    if (count <= 0 || reserved_items[owner_id][name] + count > item_count(*owner, name)) {
                        reason = "item reservation exceeds available inventory";
                        break;
                    }
                }
                if (!reason.empty()) break;
            }
        }
        if (reason.empty() &&
            (used_actors.count(candidate.action_key) != 0 ||
             (controller && used_actors.count(*controller) != 0))) {
            reason = "actor is already reserved by another action";
        }
        if (reason.empty() && move_destination && contains_pos(move_destinations, *move_destination)) {
            reason = "move destination is already reserved";
        }

        if (!reason.empty()) {
            result.rejected.push_back({candidate.source, std::move(reason)});
            continue;
        }

        result.decision.role_commands[candidate.action_key] = candidate.command;
        used_actors.insert(candidate.action_key);
        ++result.actor_uses[candidate.action_key];
        if (controller) {
            used_actors.insert(*controller);
            ++result.actor_uses[*controller];
        }
        if (move_destination) move_destinations.push_back(*move_destination);
        result.reserved_gold += candidate.reservation.gold;
        for (const auto& [owner_id, items] : candidate.reservation.items) {
            for (const auto& [name, count] : items) reserved_items[owner_id][name] += count;
        }
    }

    std::vector<std::size_t> top_order(top_level.size());
    for (std::size_t index = 0; index < top_order.size(); ++index) top_order[index] = index;
    std::stable_sort(top_order.begin(), top_order.end(), [&](std::size_t left, std::size_t right) {
        return top_level[left].priority > top_level[right].priority;
    });

    bool prompt_selected = false;
    bool command_selected = false;
    for (const std::size_t index : top_order) {
        const auto& candidate = top_level[index];
        if (candidate.kind == TopLevelKind::prompt) {
            if (prompt_selected) continue;
            if (candidate.requires_active_task && !rules.task_active) continue;
            if (!rules.task_active && rules.daily_llm_remaining <= 0) continue;
            result.decision.prompt = candidate.value;
            prompt_selected = true;
            continue;
        }

        if (command_selected || !rules.task_active ||
            (candidate.requires_active_task && !rules.task_active)) {
            continue;
        }
        const std::optional<int> pioneer = candidate.pioneer_id ? candidate.pioneer_id : rules.pioneer_id;
        if (pioneer) {
            const auto action = result.decision.role_commands.find(*pioneer);
            if (action != result.decision.role_commands.end() &&
                (action->second.action == "move" || action->second.action == "submitAnswer")) {
                continue;
            }
        }
        result.decision.execute_command = candidate.value;
        command_selected = true;
    }
    return result;
}

}  // namespace astra
