#include "actions.hpp"
#include "test_support.hpp"

#include <fstream>
#include <string>

namespace {

astra::TurnObservation action_turn(int round = 71, int gold = 75) {
    std::ifstream input("tests/fixtures/minimal_turn.json");
    astra::test::require(input.good(), "minimal_turn.json must be readable");
    auto json = nlohmann::json::parse(input);
    json["roundNo"] = round;
    json["teamOur"]["goldNum"] = gold;
    json["teamOur"]["roles"][0]["pos"] = {{"x", 20}, {"y", 20}};
    json["teamOur"]["roles"].push_back({
        {"id", 10020}, {"pos", {{"x", 9}, {"y", 24}}}, {"roleType", "gatling"},
        {"health", 1000}, {"attackRange", 5}, {"level", 2}, {"cooldown", 0},
    });
    json["teamOur"]["roles"].push_back({
        {"id", 10030}, {"pos", {{"x", 10}, {"y", 24}}}, {"roleType", "railgun"},
        {"health", 1000}, {"attackRange", 6}, {"level", 1}, {"cooldown", 0},
    });
    const auto parsed = astra::parse_turn(json);
    astra::test::require(parsed.turn.has_value() && parsed.errors.empty(),
                         "action fixture must parse");
    return *parsed.turn;
}

astra::CandidateAction move(int actor, astra::Pos target, int priority) {
    astra::CandidateAction candidate;
    candidate.action_key = actor;
    candidate.command.action = "move";
    candidate.command.target_positions = {target};
    candidate.priority = priority;
    return candidate;
}

astra::CandidateAction attack(int weapon,
                              int controller,
                              std::vector<astra::Pos> targets,
                              int priority) {
    astra::CandidateAction candidate;
    candidate.action_key = weapon;
    candidate.command.action = "attack";
    candidate.command.controller_id = std::to_string(controller);
    candidate.command.target_positions = std::move(targets);
    candidate.priority = priority;
    return candidate;
}

}  // namespace

ASTRA_TEST(actions_prevent_controller_double_use) {
    const auto turn = action_turn();
    const auto result = astra::arbitrate(
        turn,
        {attack(10020, 10010, {{9, 21}, {12, 24}}, 100), move(10010, {8, 25}, 50)},
        {},
        {});
    astra::test::require(result.actor_use_count(10010) == 1,
                         "attack and move must not use the same controller");
    astra::test::require(result.decision.role_commands.count(10020) == 1,
                         "higher-priority tower attack must win");
    astra::test::require(result.decision.role_commands.count(10010) == 0,
                         "controller move must be rejected");

    const auto two_towers = astra::arbitrate(
        turn,
        {attack(10020, 10010, {{9, 21}, {12, 24}}, 100),
         attack(10030, 10010, {{10, 20}}, 90)},
        {},
        {});
    astra::test::require(two_towers.actor_use_count(10010) == 1,
                         "one role must not control two weapons");
}

ASTRA_TEST(actions_reserve_shared_gold_and_inventory) {
    const auto turn = action_turn(20, 25);
    astra::ArbitrationRules rules;
    rules.weapon_build_tiles = {{8, 24}, {11, 25}};

    astra::CandidateAction first;
    first.action_key = 10010;
    first.command.action = "build";
    first.command.name = "gatling";
    first.command.target_positions = {{8, 24}};
    first.reservation.gold = 25;
    first.priority = 100;
    astra::CandidateAction second = first;
    second.action_key = 10012;
    second.command.target_positions = {{11, 25}};
    second.priority = 90;

    const auto builds = astra::arbitrate(turn, {first, second}, {}, rules);
    astra::test::require(builds.gold_reserved() == 25,
                         "accepted actions must fit shared gold");
    astra::test::require(builds.decision.role_commands.size() == 1,
                         "only one 25-gold build may be accepted with 25 gold");

    auto item_turn = turn;
    for (auto& unit : item_turn.team_our.roles) {
        if (unit.id == 10010) unit.backpack = {"WeaponUpgradeVoucher1"};
    }
    astra::CandidateAction use_one;
    use_one.action_key = 10010;
    use_one.command.action = "use";
    use_one.command.name = "WeaponUpgradeVoucher1";
    use_one.command.target_positions = {{9, 24}};
    use_one.reservation.items[10010]["WeaponUpgradeVoucher1"] = 1;
    use_one.priority = 100;
    auto use_two = use_one;
    use_two.command.target_positions = {{10, 24}};
    use_two.priority = 90;
    const auto uses = astra::arbitrate(item_turn, {use_one, use_two}, {}, {});
    astra::test::require(uses.decision.role_commands.size() == 1,
                         "one voucher must not be consumed by two actions");
    astra::test::require(!uses.rejected.empty() &&
                             uses.rejected.front().reason.find("inventory") != std::string::npos,
                         "duplicate voucher use must be rejected by item reservation");
}

ASTRA_TEST(actions_reject_move_target_conflicts_swaps_and_station_footprint) {
    const auto turn = action_turn();
    const auto contested = astra::arbitrate(
        turn,
        {move(10010, {9, 26}, 100), move(10012, {9, 26}, 90)},
        {},
        {});
    astra::test::require(contested.decision.role_commands.size() == 1,
                         "two actors must not reserve the same destination");

    const auto swap = astra::arbitrate(
        turn,
        {move(10010, {10, 25}, 100), move(10012, {9, 25}, 90)},
        {},
        {});
    astra::test::require(swap.decision.role_commands.empty(),
                         "position swaps must be rejected conservatively");

    auto station_turn = turn;
    station_turn.team_our.roles.front().pos = {10, 24};
    const auto station =
        astra::arbitrate(station_turn, {move(10011, {11, 25}, 100)}, {}, {});
    astra::test::require(station.decision.role_commands.empty(),
                         "all four station footprint cells must block movement");
}

ASTRA_TEST(actions_enforce_day_night_cooldown_range_and_gatling_angle) {
    const auto day_end = astra::arbitrate(
        action_turn(70), {attack(10020, 10010, {{9, 21}, {12, 24}}, 100)}, {}, {});
    astra::test::require(day_end.decision.role_commands.empty(),
                         "round 70 must still be daytime");
    const auto night_start = astra::arbitrate(
        action_turn(71), {attack(10020, 10010, {{9, 21}, {12, 24}}, 100)}, {}, {});
    astra::test::require(night_start.decision.role_commands.size() == 1,
                         "round 71 must start nighttime and allow a 90-degree boundary");
    const auto night_end = astra::arbitrate(
        action_turn(130), {attack(10030, 10010, {{10, 20}}, 100)}, {}, {});
    astra::test::require(night_end.decision.role_commands.size() == 1,
                         "round 130 must still be nighttime");
    const auto next_day = astra::arbitrate(
        action_turn(131), {attack(10030, 10010, {{10, 20}}, 100)}, {}, {});
    astra::test::require(next_day.decision.role_commands.empty(),
                         "round 131 must begin daytime");

    const auto wide_angle = astra::arbitrate(
        action_turn(71), {attack(10020, 10010, {{6, 24}, {12, 24}}, 100)}, {}, {});
    astra::test::require(wide_angle.decision.role_commands.empty(),
                         "gatling targets wider than 90 degrees must be rejected");
    const auto out_of_range = astra::arbitrate(
        action_turn(71), {attack(10030, 10010, {{10, 17}}, 100)}, {}, {});
    astra::test::require(out_of_range.decision.role_commands.empty(),
                         "targets outside explicit weapon range must be rejected");
}

ASTRA_TEST(actions_allow_duplicate_rocket_targets_but_require_explicit_cooldown) {
    auto turn = action_turn();
    astra::UnitObservation rocket;
    rocket.id = 10040;
    rocket.pos = {8, 24};
    rocket.role_type = astra::RoleType::rocket;
    rocket.role_type_raw = "rocket";
    rocket.health = 1000;
    rocket.attack_range = 10;
    rocket.level = 2;
    rocket.cooldown = 0;
    rocket.owned = true;
    turn.team_our.roles.push_back(rocket);

    const auto duplicate = astra::arbitrate(
        turn, {attack(10040, 10010, {{8, 18}, {8, 18}}, 100)}, {}, {});
    astra::test::require(duplicate.decision.role_commands.size() == 1,
                         "duplicate rocket targets are representable and legal");
    turn.team_our.roles.back().cooldown.reset();
    const auto unknown = astra::arbitrate(
        turn, {attack(10040, 10010, {{8, 18}, {8, 18}}, 100)}, {}, {});
    astra::test::require(unknown.decision.role_commands.empty(),
                         "unknown rocket cooldown must not be treated as ready");
    turn.team_our.roles.back().cooldown = 2;
    const auto cooling = astra::arbitrate(
        turn, {attack(10040, 10010, {{8, 18}, {8, 18}}, 100)}, {}, {});
    astra::test::require(cooling.decision.role_commands.empty(),
                         "active rocket cooldown must be rejected");
}

ASTRA_TEST(actions_report_missing_fields_before_unsupported_action) {
    const auto turn = action_turn(20);
    astra::CandidateAction buy;
    buy.action_key = 10010;
    buy.command.action = "buy";
    const auto result = astra::arbitrate(turn, {buy}, {}, {});
    astra::test::require(result.decision.role_commands.empty(),
                         "unsupported action must not pass through");
    astra::test::require(!result.rejected.empty() &&
                             result.rejected.front().reason.find("name") != std::string::npos,
                         "missing required field must be reported before unsupported status");
}

ASTRA_TEST(actions_reject_unknown_build_area_and_choose_one_top_level_request) {
    const auto turn = action_turn(20);
    astra::CandidateAction build;
    build.action_key = 10010;
    build.command.action = "build";
    build.command.name = "gatling";
    build.command.target_positions = {{8, 24}};
    build.reservation.gold = 25;
    build.priority = 100;
    const auto unknown_area = astra::arbitrate(turn, {build}, {}, {});
    astra::test::require(unknown_area.decision.role_commands.empty(),
                         "build must be rejected until its build area is known");

    astra::ArbitrationRules rules;
    rules.daily_llm_remaining = 1;
    const std::vector<astra::TopLevelCandidate> requests = {
        {astra::TopLevelKind::prompt, "分析新闻", 50, false, std::nullopt},
        {astra::TopLevelKind::prompt, "解决任务", 100, false, std::nullopt},
    };
    const auto top = astra::arbitrate(turn, {}, requests, rules);
    astra::test::require(top.decision.prompt == std::optional<std::string>("解决任务"),
                         "only the highest-priority prompt may be sent");
}

ASTRA_TEST(actions_drop_task_command_when_pioneer_moves_or_submits) {
    auto turn = action_turn();
    turn.phase_task = "active task";
    astra::ArbitrationRules rules;
    rules.task_active = true;
    rules.pioneer_id = 10011;
    const astra::TopLevelCandidate command{
        astra::TopLevelKind::execute_command, "python3 solve.py", 100, true, 10011};

    const auto moving = astra::arbitrate(
        turn, {move(10011, {12, 23}, 100)}, {command}, rules);
    astra::test::require(!moving.decision.execute_command.has_value(),
                         "task command must stop when pioneer moves");

    astra::CandidateAction submit;
    submit.action_key = 10011;
    submit.command.action = "submitAnswer";
    submit.command.task_answer = "answer";
    submit.priority = 100;
    const auto submitting = astra::arbitrate(turn, {submit}, {command}, rules);
    astra::test::require(!submitting.decision.execute_command.has_value(),
                         "task command must stop when pioneer submits an answer");
}

ASTRA_TEST(actions_validate_collect_daylight_target_and_health) {
    auto turn = action_turn(70);
    turn.map.zones.push_back({{8, 24}, "stone"});
    astra::CandidateAction collect;
    collect.action_key = 10010;
    collect.command.action = "collect";
    collect.command.target_positions = {{8, 24}};
    collect.priority = 100;

    const auto valid = astra::arbitrate(turn, {collect}, {}, {});
    astra::test::require(valid.decision.role_commands.count(10010) == 1,
                         "living worker must collect adjacent mineral at round 70");

    turn.round_no = 71;
    astra::test::require(astra::arbitrate(turn, {collect}, {}, {}).decision.role_commands.empty(),
                         "collect must stop at night start");
    turn.round_no = 70;
    for (auto health : {std::optional<int>{}, std::optional<int>{0}, std::optional<int>{-1}}) {
        auto unhealthy = turn;
        for (auto& unit : unhealthy.team_our.roles) {
            if (unit.id == 10010) unit.health = health;
        }
        astra::test::require(
            astra::arbitrate(unhealthy, {collect}, {}, {}).decision.role_commands.empty(),
            "collect must reject missing or non-positive health");
    }
}

ASTRA_TEST(actions_validate_sell_quantity_inventory_and_owner_reservation) {
    auto turn = action_turn(20);
    turn.map.zones.push_back({{8, 24}, "vendor"});
    for (auto& unit : turn.team_our.roles) {
        if (unit.id == 10010) unit.backpack = {"stone", "stone", "iron"};
    }
    astra::CandidateAction sell;
    sell.action_key = 10010;
    sell.command.action = "sell";
    sell.command.name = "stone";
    sell.command.number = 2;
    sell.reservation.items[10010]["stone"] = 2;
    sell.priority = 100;

    const auto valid = astra::arbitrate(turn, {sell}, {}, {});
    astra::test::require(valid.decision.role_commands.count(10010) == 1,
                         "seller must batch-sell owned minerals beside vendor");

    auto overdrawn = sell;
    overdrawn.command.number = 3;
    overdrawn.reservation.items[10010]["stone"] = 3;
    astra::test::require(
        astra::arbitrate(turn, {overdrawn}, {}, {}).decision.role_commands.empty(),
        "sell quantity must not exceed selling actor inventory");
    auto wrong_owner = sell;
    wrong_owner.reservation.items.clear();
    wrong_owner.reservation.items[10012]["stone"] = 2;
    astra::test::require(
        astra::arbitrate(turn, {wrong_owner}, {}, {}).decision.role_commands.empty(),
        "sell reservation owner must be the selling actor");
}

ASTRA_TEST(actions_validate_accept_task_owner_cooldown_and_health) {
    auto turn = action_turn(20);
    turn.team_our.player_tasks.front().position = {12, 24};
    turn.team_our.player_tasks.front().cooldown_rounds = 0;
    turn.team_our.player_tasks.front().valid = true;
    astra::CandidateAction accept;
    accept.action_key = 10011;
    accept.command.action = "acceptTask";
    accept.priority = 100;

    astra::test::require(
        astra::arbitrate(turn, {accept}, {}, {}).decision.role_commands.count(10011) == 1,
        "living pioneer beside own ready task point must accept task");

    turn.team_our.player_tasks.front().cooldown_rounds = 1;
    astra::test::require(
        astra::arbitrate(turn, {accept}, {}, {}).decision.role_commands.empty(),
        "cooling task point must reject acceptance");
    turn.team_our.player_tasks.front().cooldown_rounds = 0;
    turn.team_our.player_tasks.front().valid = false;
    astra::test::require(
        astra::arbitrate(turn, {accept}, {}, {}).decision.role_commands.empty(),
        "invalid own task point must reject acceptance");
    turn.team_our.player_tasks.front().valid = true;
    for (auto health : {std::optional<int>{}, std::optional<int>{0}, std::optional<int>{-1}}) {
        auto unhealthy = turn;
        for (auto& unit : unhealthy.team_our.roles) {
            if (unit.id == 10011) unit.health = health;
        }
        astra::test::require(
            astra::arbitrate(unhealthy, {accept}, {}, {}).decision.role_commands.empty(),
            "acceptTask must reject missing or non-positive health");
    }
}
