#include "combat.hpp"
#include "navigation.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace astra {
namespace {

int distance(const Pos& left, const Pos& right) {
    return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
}

bool is_night(int round_no) {
    return (round_no - 1) % 130 + 1 > 70;
}

bool is_weapon(RoleType type) {
    return type == RoleType::gatling || type == RoleType::railgun ||
           type == RoleType::rocket;
}

bool is_character(RoleType type) {
    return type == RoleType::worker || type == RoleType::pioneer;
}

bool alive(const UnitObservation& unit) {
    return unit.owned && unit.health && *unit.health > 0;
}

bool in_bounds(const TurnObservation& turn, const Pos& pos) {
    return pos.x >= 0 && pos.y >= 0 && pos.x < turn.map.width && pos.y < turn.map.height;
}

int robot_attack(const std::string& type) {
    if (type == "smallRobot") return 5;
    if (type == "middleRobot") return 10;
    if (type == "largeRobot") return 20;
    if (type == "bossRobot") return 40;
    return 0;
}

int robot_points(const std::string& type) {
    if (type == "smallRobot") return 1;
    if (type == "middleRobot") return 2;
    if (type == "largeRobot") return 4;
    if (type == "bossRobot") return 10;
    return 0;
}

bool targets_our_team(const TurnObservation& turn, const RobotObservation& robot) {
    return robot.target_team && *robot.target_team == turn.team_our.type;
}

int station_distance(const TurnObservation& turn, const Pos& pos) {
    int best = turn.map.width + turn.map.height;
    for (const auto& unit : turn.team_our.roles) {
        if (unit.role_type != RoleType::station || !alive(unit)) continue;
        for (const auto& cell : occupied_cells(unit)) {
            best = std::min(best, distance(pos, cell));
        }
    }
    return best;
}

long long damage_utility(const TurnObservation& turn,
                         const RobotObservation& robot,
                         int health,
                         int damage) {
    if (health <= 0 || damage <= 0) return 0;
    const int dealt = std::min(health, damage);
    const int threat_multiplier = targets_our_team(turn, robot) ? 4 : 1;
    const int proximity = std::max(0, 24 - station_distance(turn, robot.pos));
    long long result = static_cast<long long>(dealt) *
                       (20 + robot_attack(robot.role_type) + proximity) * threat_multiplier;
    if (dealt == health) {
        result += static_cast<long long>(robot_points(robot.role_type)) * 4000;
    }
    return result;
}

bool complete_weapon(const UnitObservation& weapon) {
    return alive(weapon) && is_weapon(weapon.role_type) && weapon.level &&
           *weapon.level >= 1 && *weapon.level <= 3 && weapon.attack_range &&
           *weapon.attack_range > 0 && weapon.attack_power && *weapon.attack_power > 0 &&
           (weapon.role_type != RoleType::rocket || (weapon.cooldown && *weapon.cooldown == 0));
}

std::vector<const RobotObservation*> live_robots(const TurnObservation& turn) {
    std::vector<const RobotObservation*> result;
    for (const auto& robot : turn.robots) {
        if (robot.health > 0) result.push_back(&robot);
    }
    std::sort(result.begin(), result.end(), [](const RobotObservation* left, const RobotObservation* right) {
        return left->id < right->id;
    });
    return result;
}

using RemainingHealth = std::map<int, int>;

int remaining_health(const RemainingHealth& health, const RobotObservation& robot) {
    const auto found = health.find(robot.id);
    return found == health.end() ? robot.health : found->second;
}

astra::Optional<AttackPlan> rocket_plan(const TurnObservation& turn,
                                      const UnitObservation& weapon,
                                      const RemainingHealth& previous) {
    const auto robots = live_robots(turn);
    std::set<std::pair<int, int>> cells;
    for (const auto* robot : robots) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const Pos target{robot->pos.x + dx, robot->pos.y + dy};
                const bool hits_opponent_wave = std::any_of(
                    robots.begin(), robots.end(), [&](const RobotObservation* other) {
                        return other->target_team &&
                               *other->target_team == (turn.team_our.type == "challenger" ? "defender" : "challenger") &&
                               distance(target, other->pos) <= 1;
                    });
                if (in_bounds(turn, target) && distance(weapon.pos, target) > 0 &&
                    distance(weapon.pos, target) <= *weapon.attack_range && !hits_opponent_wave) {
                    cells.emplace(target.x, target.y);
                }
            }
        }
    }
    if (cells.empty()) return astra::nullopt;

    std::map<int, int> health;
    for (const auto* robot : robots) health[robot->id] = remaining_health(previous, *robot);
    AttackPlan plan;
    for (int missile = 0; missile < *weapon.level; ++missile) {
        long long best_score = std::numeric_limits<long long>::min();
        Pos best{};
        int best_affected = 0;
        for (const auto& x_entry : cells) {
            const auto& x = x_entry.first;
            const auto& y = x_entry.second;
            const Pos target{x, y};
            long long score = 0;
            int affected = 0;
            for (const auto* robot : robots) {
                const int splash_distance = distance(target, robot->pos);
                if (splash_distance > 1) continue;
                const int damage = splash_distance == 0 ? 20 : 10;
                score += damage_utility(turn, *robot, health[robot->id], damage);
                if (health[robot->id] > 0) {
                    ++affected;
                }
            }
            score += affected * 5LL;
            if (score > best_score ||
                (score == best_score && std::tie(x, y) < std::tie(best.x, best.y))) {
                best_score = score;
                best = target;
                best_affected = affected;
            }
        }
        if (missile == 0 && (best_score <= 0 || best_affected == 0)) return astra::nullopt;
        if (best_score <= 0) best = plan.targets.front();
        plan.targets.push_back(best);
        if (best_score > 0) plan.utility += best_score;
        for (const auto* robot : robots) {
            const int splash_distance = distance(best, robot->pos);
            if (splash_distance <= 1) {
                const int damage = splash_distance == 0 ? 20 : 10;
                health[robot->id] = std::max(0, health[robot->id] - damage);
            }
        }
    }
    return plan;
}

bool on_segment(const Pos& origin, const Pos& endpoint, const Pos& point) {
    const long long ex = endpoint.x - origin.x;
    const long long ey = endpoint.y - origin.y;
    const long long px = point.x - origin.x;
    const long long py = point.y - origin.y;
    if (ex * py - ey * px != 0) return false;
    const long long dot = ex * px + ey * py;
    return dot > 0 && dot <= ex * ex + ey * ey;
}

astra::Optional<AttackPlan> railgun_plan(const TurnObservation& turn,
                                       const UnitObservation& weapon,
                                       const RemainingHealth& previous) {
    const auto robots = live_robots(turn);
    AttackPlan best;
    bool found = false;
    for (const auto* endpoint_robot : robots) {
        if (distance(weapon.pos, endpoint_robot->pos) > *weapon.attack_range) {
            continue;
        }
        std::vector<const RobotObservation*> line;
        for (const auto* robot : robots) {
            if (on_segment(weapon.pos, endpoint_robot->pos, robot->pos)) line.push_back(robot);
        }
        std::sort(line.begin(), line.end(), [&](const RobotObservation* left, const RobotObservation* right) {
            const int left_distance = distance(weapon.pos, left->pos);
            const int right_distance = distance(weapon.pos, right->pos);
            return left_distance != right_distance ? left_distance < right_distance : left->id < right->id;
        });
        int energy = *weapon.attack_power;
        long long utility = 0;
        for (const auto* robot : line) {
            const int damage = std::min(energy, robot->health);
            utility += damage_utility(turn, *robot, remaining_health(previous, *robot), damage);
            energy -= damage;
            if (energy == 0) break;
        }
        if (utility > 0 &&
            (!found || utility > best.utility ||
             (utility == best.utility &&
              std::tie(endpoint_robot->pos.x, endpoint_robot->pos.y) <
                  std::tie(best.targets[0].x, best.targets[0].y)))) {
            best.targets = {endpoint_robot->pos};
            best.utility = utility;
            found = true;
        }
    }
    return found ? astra::Optional<AttackPlan>(best) : astra::nullopt;
}

const RobotObservation* gatling_hit(const std::vector<const RobotObservation*>& robots,
                                    const Pos& origin,
                                    const Pos& target) {
    const RobotObservation* hit = nullptr;
    for (const auto* robot : robots) {
        if (!on_segment(origin, target, robot->pos)) continue;
        if (!hit || distance(origin, robot->pos) < distance(origin, hit->pos) ||
            (distance(origin, robot->pos) == distance(origin, hit->pos) && robot->id < hit->id)) {
            hit = robot;
        }
    }
    return hit;
}

bool cone_compatible(const Pos& origin, const Pos& target, const std::vector<Pos>& selected) {
    const long long tx = target.x - origin.x;
    const long long ty = target.y - origin.y;
    for (const auto& other : selected) {
        const long long ox = other.x - origin.x;
        const long long oy = other.y - origin.y;
        if (tx * ox + ty * oy < 0) return false;
    }
    return true;
}

bool target_sequence_less(const std::vector<Pos>& left, const std::vector<Pos>& right) {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](const Pos& a, const Pos& b) {
            return std::tie(a.x, a.y) < std::tie(b.x, b.y);
        });
}

astra::Optional<AttackPlan> gatling_plan(const TurnObservation& turn,
                                       const UnitObservation& weapon,
                                       const RemainingHealth& previous) {
    const auto robots = live_robots(turn);
    std::vector<Pos> targets;
    for (const auto* robot : robots) {
        if (distance(weapon.pos, robot->pos) <= *weapon.attack_range) {
            targets.push_back(robot->pos);
        }
    }
    std::sort(targets.begin(), targets.end(), [](const Pos& left, const Pos& right) {
        return std::tie(left.x, left.y) < std::tie(right.x, right.y);
    });
    targets.erase(std::unique(targets.begin(), targets.end(), [](const Pos& left, const Pos& right) {
                      return left.x == right.x && left.y == right.y;
                  }),
                  targets.end());
    if (targets.empty()) return astra::nullopt;

    AttackPlan best;
    bool found = false;
    for (const auto& seed : targets) {
        AttackPlan plan;
        std::map<int, int> health;
        for (const auto* robot : robots) health[robot->id] = remaining_health(previous, *robot);
        for (int bullet = 0; bullet < *weapon.level; ++bullet) {
            long long best_score = std::numeric_limits<long long>::min();
            Pos best_target{};
            const RobotObservation* best_hit = nullptr;
            for (const auto& target : targets) {
                if ((bullet == 0 && (target.x != seed.x || target.y != seed.y)) ||
                    !cone_compatible(weapon.pos, target, plan.targets)) {
                    continue;
                }
                const auto* hit = gatling_hit(robots, weapon.pos, target);
                if (!hit) continue;
                const long long score =
                    damage_utility(turn, *hit, health[hit->id], 10);
                if (score > best_score ||
                    (score == best_score &&
                     std::tie(target.x, target.y) < std::tie(best_target.x, best_target.y))) {
                    best_score = score;
                    best_target = target;
                    best_hit = hit;
                }
            }
            if (!best_hit) break;
            plan.targets.push_back(best_target);
            if (best_score > 0) plan.utility += best_score;
            health[best_hit->id] = std::max(0, health[best_hit->id] - 10);
        }
        if (plan.targets.size() == static_cast<std::size_t>(*weapon.level) && plan.utility > 0 &&
            (!found || plan.utility > best.utility ||
             (plan.utility == best.utility && target_sequence_less(plan.targets, best.targets)))) {
            best = std::move(plan);
            found = true;
        }
    }
    return found ? astra::Optional<AttackPlan>(best) : astra::nullopt;
}

struct WeaponOption {
    const UnitObservation* weapon = nullptr;
    AttackPlan plan;
    std::vector<const UnitObservation*> controllers;
};

astra::Optional<AttackPlan> plan_attack(const TurnObservation& turn,
                                      const UnitObservation& weapon,
                                      const RemainingHealth& previous) {
    if (!is_night(turn.round_no) || !complete_weapon(weapon)) return astra::nullopt;
    if (weapon.role_type == RoleType::rocket) return rocket_plan(turn, weapon, previous);
    if (weapon.role_type == RoleType::railgun) return railgun_plan(turn, weapon, previous);
    return gatling_plan(turn, weapon, previous);
}

void reserve_damage(const TurnObservation& turn,
                     const UnitObservation& weapon,
                     const AttackPlan& plan,
                     RemainingHealth& health) {
    const auto robots = live_robots(turn);
    const auto damage = [&](const RobotObservation& robot, int amount) {
        // C++11 may evaluate operator[] first and insert zero before reading health.
        const int after_damage = std::max(0, remaining_health(health, robot) - amount);
        health[robot.id] = after_damage;
    };
    for (const auto& target : plan.targets) {
        if (weapon.role_type == RoleType::rocket) {
            for (const auto* robot : robots) {
                const int range = distance(target, robot->pos);
                if (range <= 1) damage(*robot, range == 0 ? 20 : 10);
            }
        } else if (weapon.role_type == RoleType::gatling) {
            const auto* hit = gatling_hit(robots, weapon.pos, target);
            if (hit) damage(*hit, 10);
        } else {
            std::vector<const RobotObservation*> line;
            for (const auto* robot : robots) {
                if (on_segment(weapon.pos, target, robot->pos)) line.push_back(robot);
            }
            std::sort(line.begin(), line.end(), [&](const RobotObservation* left, const RobotObservation* right) {
                return std::make_pair(distance(weapon.pos, left->pos), left->id) <
                       std::make_pair(distance(weapon.pos, right->pos), right->id);
            });
            int energy = *weapon.attack_power;
            for (const auto* robot : line) {
                // Damage settles at round end: planned kills still block shots and consume energy.
                const int amount = std::min(energy, robot->health);
                damage(*robot, amount);
                energy -= amount;
                if (energy == 0) break;
            }
        }
    }
}

struct Matching {
    long long utility = 0;
    std::vector<std::pair<int, int>> assignments;
};

bool better_matching(const Matching& left, const Matching& right) {
    if (left.utility != right.utility) return left.utility > right.utility;
    if (left.assignments.size() != right.assignments.size()) {
        return left.assignments.size() > right.assignments.size();
    }
    return left.assignments < right.assignments;
}

void search_matching(const std::vector<WeaponOption>& options,
                     std::size_t index,
                     std::set<int>& used_controllers,
                     Matching current,
                     Matching& best) {
    if (index == options.size()) {
        if (better_matching(current, best)) best = std::move(current);
        return;
    }
    search_matching(options, index + 1, used_controllers, current, best);
    const auto& option = options[index];
    for (const auto* controller : option.controllers) {
        if (used_controllers.count(controller->id) != 0) continue;
        used_controllers.insert(controller->id);
        current.utility += option.plan.utility;
        current.assignments.emplace_back(option.weapon->id, controller->id);
        search_matching(options, index + 1, used_controllers, current, best);
        current.assignments.pop_back();
        current.utility -= option.plan.utility;
        used_controllers.erase(controller->id);
    }
}

}  // namespace

astra::Optional<AttackPlan> plan_weapon_attack(const TurnObservation& turn,
                                             const UnitObservation& weapon) {
    return plan_attack(turn, weapon, {});
}

std::vector<CandidateAction> combat_candidates(const TurnObservation& turn, int priority_start) {
    if (!is_night(turn.round_no)) return {};
    std::vector<const UnitObservation*> controllers;
    for (const auto& unit : turn.team_our.roles) {
        if (alive(unit) && is_character(unit.role_type) &&
            (unit.role_type != RoleType::pioneer || turn.phase_task.empty())) {
            controllers.push_back(&unit);
        }
    }
    std::sort(controllers.begin(), controllers.end(), [](const UnitObservation* left, const UnitObservation* right) {
        return left->id < right->id;
    });

    std::vector<WeaponOption> options;
    for (const auto& unit : turn.team_our.roles) {
        const auto plan = plan_weapon_attack(turn, unit);
        if (!plan) continue;
        WeaponOption option;
        option.weapon = &unit;
        option.plan = *plan;
        for (const auto* controller : controllers) {
            if (distance(unit.pos, controller->pos) <= 1) option.controllers.push_back(controller);
        }
        if (!option.controllers.empty()) options.push_back(std::move(option));
    }
    std::sort(options.begin(), options.end(), [](const WeaponOption& left, const WeaponOption& right) {
        return left.weapon->id < right.weapon->id;
    });

    Matching best;
    std::set<int> used_controllers;
    search_matching(options, 0, used_controllers, {}, best);

    std::vector<CandidateAction> result;
    std::map<int, const WeaponOption*> option_by_id;
    for (const auto& option : options) option_by_id[option.weapon->id] = &option;
    std::sort(best.assignments.begin(), best.assignments.end(), [&](const std::pair<int, int>& left, const std::pair<int, int>& right) {
        return std::make_pair(*option_by_id.at(left.first)->weapon->attack_range, left.first) <
               std::make_pair(*option_by_id.at(right.first)->weapon->attack_range, right.first);
    });
    RemainingHealth health;
    int priority = priority_start;
    for (const auto& weapon_id_entry : best.assignments) {
        const auto& weapon_id = weapon_id_entry.first;
        const auto& controller_id = weapon_id_entry.second;
        const auto& option = *option_by_id.at(weapon_id);
        const auto plan = plan_attack(turn, *option.weapon, health);
        if (!plan) continue;
        CandidateAction candidate;
        candidate.action_key = weapon_id;
        candidate.command.action = "attack";
        candidate.command.controller_id = std::to_string(controller_id);
        candidate.command.target_positions = plan->targets;
        candidate.priority = priority--;
        candidate.source = "combat.attack";
        result.push_back(std::move(candidate));
        reserve_damage(turn, *option.weapon, *plan, health);
    }
    return result;
}

}  // namespace astra
