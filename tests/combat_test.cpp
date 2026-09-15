#include "combat.hpp"
#include "test_support.hpp"

#include <set>
#include <string>

namespace {

astra::UnitObservation unit(int id, astra::Pos pos, astra::RoleType type) {
    astra::UnitObservation result;
    result.id = id;
    result.pos = pos;
    result.role_type = type;
    result.health = 1000;
    result.owned = true;
    return result;
}

astra::UnitObservation weapon(int id,
                              astra::Pos pos,
                              astra::RoleType type,
                              int level,
                              int range,
                              int power) {
    auto result = unit(id, pos, type);
    result.level = level;
    result.attack_range = range;
    result.attack_power = power;
    result.cooldown = 0;
    return result;
}

astra::RobotObservation robot(int id,
                              astra::Pos pos,
                              const std::string& type,
                              int health,
                              std::optional<std::string> target = "challenger") {
    return {id, pos, type, health, "", std::move(target)};
}

astra::TurnObservation combat_turn() {
    astra::TurnObservation turn;
    turn.round_no = 71;
    turn.map.width = 41;
    turn.map.height = 32;
    turn.team_our.type = "challenger";
    turn.team_our.team_id = "astra-test";
    turn.team_our.team_name = "Astra";
    turn.team_our.roles.push_back(unit(10013, {10, 24}, astra::RoleType::station));
    return turn;
}

}  // namespace

ASTRA_TEST(combat_attacks_any_pve_robot_but_prioritizes_our_immediate_threat) {
    auto turn = combat_turn();
    const auto rocket = weapon(10040, {10, 10}, astra::RoleType::rocket, 1, 10, 20);
    turn.robots = {
        robot(1, {12, 10}, "smallRobot", 20, "defender"),
        robot(2, {15, 10}, "smallRobot", 20, "challenger"),
    };

    const auto night = astra::plan_weapon_attack(turn, rocket);
    astra::test::require(night && night->targets.size() == 1,
                         "night weapon must receive a legal target");
    astra::test::require(std::max(std::abs(night->targets[0].x - 15),
                                 std::abs(night->targets[0].y - 10)) <= 1,
                         "strategy must prioritize the robot threatening our own base");

    turn.robots.erase(turn.robots.begin() + 1);
    const auto remaining = astra::plan_weapon_attack(turn, rocket);
    astra::test::require(remaining &&
                             std::max(std::abs(remaining->targets[0].x - 12),
                                      std::abs(remaining->targets[0].y - 10)) <= 1,
                         "PVE scoring must still attack robots with another targetTeam");

    turn.round_no = 70;
    astra::test::require(!astra::plan_weapon_attack(turn, rocket),
                         "weapons must remain idle during daylight");
}

ASTRA_TEST(combat_rocket_uses_empty_splash_cell_for_cluster) {
    auto turn = combat_turn();
    const auto rocket = weapon(10040, {1, 1}, astra::RoleType::rocket, 1, 10, 20);
    turn.robots = {
        robot(1, {5, 5}, "smallRobot", 40),
        robot(2, {7, 5}, "smallRobot", 40),
    };

    const auto plan = astra::plan_weapon_attack(turn, rocket);
    astra::test::require(plan && plan->targets.size() == 1,
                         "rocket must produce one target at level one");
    const auto target = plan->targets.front();
    astra::test::require(std::max(std::abs(target.x - 5), std::abs(target.y - 5)) <= 1 &&
                             std::max(std::abs(target.x - 7), std::abs(target.y - 5)) <= 1,
                         "rocket target must splash both robots");
}

ASTRA_TEST(combat_rocket_salvo_shares_damage_across_weapons) {
    auto turn = combat_turn();
    turn.team_our.roles.push_back(weapon(10040, {9, 25}, astra::RoleType::rocket, 3, 41, 20));
    turn.team_our.roles.push_back(weapon(10041, {10, 25}, astra::RoleType::rocket, 3, 41, 20));
    turn.team_our.roles.push_back(weapon(10042, {12, 22}, astra::RoleType::rocket, 3, 41, 20));
    turn.team_our.roles.push_back(unit(10010, {8, 25}, astra::RoleType::worker));
    turn.team_our.roles.push_back(unit(10012, {11, 26}, astra::RoleType::worker));
    turn.team_our.roles.push_back(unit(10011, {12, 23}, astra::RoleType::pioneer));
    turn.robots = {robot(1, {15, 20}, "middleRobot", 60),
                   robot(2, {21, 20}, "middleRobot", 60),
                   robot(3, {27, 20}, "middleRobot", 60)};

    const auto attacks = astra::combat_candidates(turn, 3000);
    astra::test::require(attacks.size() == 3, "three staffed rockets must fire");
    for (const auto& target : turn.robots) {
        int damage = 0;
        for (const auto& attack : attacks) {
            for (const auto& aim : attack.command.target_positions) {
                const int range = std::max(std::abs(aim.x - target.pos.x),
                                            std::abs(aim.y - target.pos.y));
                damage += range == 0 ? 20 : (range == 1 ? 10 : 0);
            }
        }
        astra::test::require(damage >= target.health,
                             "coordinated salvo must kill all three separated 60HP targets");
    }
}

ASTRA_TEST(combat_threat_distance_uses_station_top_left_footprint) {
    auto turn = combat_turn();
    const auto gun = weapon(10040, {8, 24}, astra::RoleType::rocket, 1, 10, 20);
    turn.robots = {robot(1, {10, 22}, "smallRobot", 20),
                   robot(2, {10, 26}, "smallRobot", 20)};
    const auto plan = astra::plan_weapon_attack(turn, gun);
    astra::test::require(plan && plan->targets.front().x == 10 &&
                             plan->targets.front().y == 22,
                         "robot adjacent to the base bottom edge must take priority");
}

ASTRA_TEST(combat_planned_kill_still_blocks_other_weapons_until_round_end) {
    auto turn = combat_turn();
    turn.team_our.roles = {
        weapon(10040, {2, 2}, astra::RoleType::rocket, 1, 5, 20),
        weapon(10020, {2, 3}, astra::RoleType::gatling, 1, 10, 10),
        unit(10010, {1, 2}, astra::RoleType::worker),
        unit(10012, {1, 3}, astra::RoleType::worker),
    };
    turn.robots = {robot(1, {5, 3}, "smallRobot", 20),
                   robot(2, {8, 3}, "smallRobot", 40)};
    const auto attacks = astra::combat_candidates(turn);
    astra::test::require(attacks.size() == 1 && attacks.front().action_key == 10040,
                         "gatling must not shoot through a robot whose death is only planned");
}

ASTRA_TEST(combat_does_not_assign_active_task_pioneer_to_weapon) {
    auto turn = combat_turn();
    turn.team_our.roles = {
        weapon(10040, {2, 2}, astra::RoleType::rocket, 1, 10, 20),
        unit(10011, {1, 2}, astra::RoleType::pioneer),
    };
    turn.robots = {robot(1, {5, 3}, "smallRobot", 20)};
    turn.phase_task = "active task";
    astra::test::require(astra::combat_candidates(turn).empty(),
                         "task-bound pioneer must not reserve a weapon or predicted damage");
}

ASTRA_TEST(combat_railgun_aims_through_aligned_robots) {
    auto turn = combat_turn();
    const auto railgun = weapon(10030, {1, 1}, astra::RoleType::railgun, 3, 10, 100);
    turn.robots = {
        robot(1, {3, 1}, "smallRobot", 40),
        robot(2, {6, 1}, "middleRobot", 60),
    };

    const auto plan = astra::plan_weapon_attack(turn, railgun);
    astra::test::require(plan && plan->targets.size() == 1 &&
                             plan->targets.front().x == 6 && plan->targets.front().y == 1,
                         "railgun must use the far endpoint to pierce both aligned robots");
}

ASTRA_TEST(combat_gatling_keeps_all_bullets_inside_one_cone) {
    auto turn = combat_turn();
    const auto gatling = weapon(10020, {10, 10}, astra::RoleType::gatling, 2, 5, 20);
    turn.robots = {
        robot(1, {12, 10}, "smallRobot", 10),
        robot(2, {10, 12}, "smallRobot", 10),
        robot(3, {8, 10}, "bossRobot", 800),
    };

    const auto plan = astra::plan_weapon_attack(turn, gatling);
    astra::test::require(plan && plan->targets.size() == 2,
                         "level two gatling must produce two targets");
    const auto first = plan->targets[0];
    const auto second = plan->targets[1];
    const long long ax = first.x - gatling.pos.x;
    const long long ay = first.y - gatling.pos.y;
    const long long bx = second.x - gatling.pos.x;
    const long long by = second.y - gatling.pos.y;
    astra::test::require(ax * bx + ay * by >= 0,
                         "gatling targets must remain inside the legal 90 degree cone");
}

ASTRA_TEST(combat_requires_complete_weapon_fields_and_reachable_target) {
    auto turn = combat_turn();
    auto rocket = weapon(10040, {1, 1}, astra::RoleType::rocket, 1, 3, 20);
    turn.robots = {robot(1, {10, 10}, "smallRobot", 40)};
    astra::test::require(!astra::plan_weapon_attack(turn, rocket),
                         "out-of-range robots must not produce an attack");

    rocket.attack_range.reset();
    turn.robots.front().pos = {2, 1};
    astra::test::require(!astra::plan_weapon_attack(turn, rocket),
                         "unknown weapon range must not be guessed");
}

ASTRA_TEST(combat_assigns_distinct_adjacent_controllers) {
    auto turn = combat_turn();
    turn.team_our.roles = {
        unit(10010, {9, 10}, astra::RoleType::worker),
        unit(10011, {10, 9}, astra::RoleType::pioneer),
        weapon(10020, {10, 10}, astra::RoleType::gatling, 1, 5, 10),
        weapon(10040, {11, 10}, astra::RoleType::rocket, 1, 10, 20),
        unit(10013, {20, 20}, astra::RoleType::station),
    };
    turn.team_our.roles[0].health = 220;
    turn.team_our.roles[1].health = 200;
    turn.robots = {robot(1, {14, 10}, "middleRobot", 60)};

    const auto candidates = astra::combat_candidates(turn);
    astra::test::require(candidates.size() == 2,
                         "two weapons with two controllers must both attack");
    std::set<std::string> controllers;
    for (const auto& candidate : candidates) {
        astra::test::require(candidate.command.controller_id.has_value(),
                             "attack must name its controller");
        controllers.insert(*candidate.command.controller_id);
    }
    astra::test::require(controllers.size() == 2,
                         "combat matching must not reuse one controller");
}
