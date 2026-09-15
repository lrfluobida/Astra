#include "strategy.hpp"
#include "test_support.hpp"

#include <optional>
#include <set>

namespace {

astra::UnitObservation worker(int id, astra::Pos pos) {
    astra::UnitObservation unit;
    unit.id = id;
    unit.pos = pos;
    unit.role_type = astra::RoleType::worker;
    unit.role_type_raw = "worker";
    unit.health = 220;
    unit.backpack_capacity = 100;
    unit.owned = true;
    return unit;
}

astra::UnitObservation pioneer(int id, astra::Pos pos) {
    auto unit = worker(id, pos);
    unit.role_type = astra::RoleType::pioneer;
    unit.role_type_raw = "pioneer";
    unit.backpack_capacity = 40;
    return unit;
}

astra::UnitObservation station(astra::Pos pos) {
    astra::UnitObservation unit;
    unit.id = 10013;
    unit.pos = pos;
    unit.role_type = astra::RoleType::station;
    unit.role_type_raw = "station";
    unit.health = 1500;
    unit.owned = true;
    return unit;
}

astra::UnitObservation rocket(int id, astra::Pos pos, int level) {
    auto unit = station(pos);
    unit.id = id;
    unit.role_type = astra::RoleType::rocket;
    unit.role_type_raw = "rocket";
    unit.level = level;
    unit.attack_range = level == 1 ? 10 : (level == 2 ? 15 : 41);
    unit.cooldown = 0;
    return unit;
}

astra::TurnObservation economy_turn() {
    astra::TurnObservation turn;
    turn.round_no = 10;
    turn.map.width = 12;
    turn.map.height = 10;
    turn.team_our.type = "challenger";
    turn.team_our.gold = 75;
    turn.team_our.roles.push_back(worker(10010, {1, 1}));
    turn.vendor_shop = {{"stone", 1}, {"iron", 3}, {"copper", 5}};
    return turn;
}

const astra::RoleCommand* command_for(const astra::Decision& decision, int actor) {
    const auto found = decision.role_commands.find(actor);
    return found == decision.role_commands.end() ? nullptr : &found->second;
}

}  // namespace

ASTRA_TEST(strategy_collects_best_priced_adjacent_mineral) {
    auto turn = economy_turn();
    turn.map.zones.push_back({{2, 1}, "stone"});
    turn.map.zones.push_back({{1, 2}, "copper"});
    turn.map.zones.push_back({{10, 8}, "vendor"});

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* command = command_for(decision, 10010);
    astra::test::require(command && command->action == "collect",
                         "worker beside priced mineral must collect");
    astra::test::require(command->target_positions.size() == 1 &&
                             command->target_positions.front().x == 1 &&
                             command->target_positions.front().y == 2,
                         "worker must prefer higher-priced adjacent copper");
}

ASTRA_TEST(strategy_sells_highest_total_value_mineral_in_one_batch) {
    auto turn = economy_turn();
    turn.map.zones.push_back({{2, 1}, "vendor"});
    turn.team_our.roles.front().backpack = {"copper", "copper", "stone", "stone",
                                             "stone", "stone", "stone", "stone"};

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* command = command_for(decision, 10010);
    astra::test::require(command && command->action == "sell",
                         "worker beside vendor must sell carried minerals");
    astra::test::require(command->name == std::optional<std::string>("copper") &&
                             command->number == std::optional<int>(2),
                         "sell must choose highest total value and batch full quantity");
}

ASTRA_TEST(strategy_uses_price_and_round_trip_cost_to_choose_mine) {
    auto turn = economy_turn();
    turn.team_our.roles.front().pos = {1, 4};
    turn.map.zones.push_back({{10, 4}, "vendor"});
    turn.map.zones.push_back({{2, 8}, "copper"});
    turn.map.zones.push_back({{5, 4}, "iron"});

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* command = command_for(decision, 10010);
    astra::test::require(command && command->action == "move",
                         "worker must move toward a profitable reachable mine");
    astra::test::require(command->target_positions.front().y > 4,
                         "round-trip score must prefer the more profitable copper route");
}

ASTRA_TEST(strategy_skips_departure_for_unknown_capacity_or_nonpositive_prices) {
    auto turn = economy_turn();
    turn.map.zones.push_back({{10, 8}, "vendor"});
    turn.map.zones.push_back({{5, 5}, "copper"});
    turn.team_our.roles.front().backpack_capacity.reset();
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "missing capacity must prevent mining departure");

    turn.team_our.roles.front().backpack_capacity = 0;
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "zero capacity must prevent mining departure");
    turn.team_our.roles.front().backpack_capacity = 100;
    turn.vendor_shop = {{"copper", 0}};
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "non-positive price must prevent mining departure");
    turn.vendor_shop = {{"copper", 5}};
    turn.team_our.roles.front().backpack_capacity = 1;
    turn.team_our.roles.front().backpack = {"stone", "iron"};
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "overfull inventory must prevent mining departure");
}

ASTRA_TEST(strategy_uses_mineral_rank_then_coordinate_for_exact_ties) {
    auto turn = economy_turn();
    turn.team_our.roles.front().pos = {5, 5};
    turn.map.zones = {{{5, 0}, "vendor"}, {{2, 5}, "copper"}, {{8, 5}, "iron"}};
    turn.vendor_shop = {{"copper", 3}, {"iron", 3}};
    const auto ranked = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(ranked, 10010)->target_positions.front().x < 5,
                         "copper must win an otherwise exact tie with iron");

    turn.map.zones = {{{5, 0}, "vendor"}, {{8, 5}, "stone"}, {{2, 5}, "stone"}};
    turn.vendor_shop = {{"stone", 1}};
    const auto coordinate = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(coordinate, 10010)->target_positions.front().x < 5,
                         "coordinate order must break an exact same-mineral tie");
}

ASTRA_TEST(strategy_reserves_distinct_next_cells_for_two_workers) {
    auto turn = economy_turn();
    turn.team_our.roles.push_back(worker(10012, {1, 3}));
    turn.map.zones.push_back({{10, 2}, "vendor"});
    turn.map.zones.push_back({{8, 2}, "copper"});

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* first = command_for(decision, 10010);
    const auto* second = command_for(decision, 10012);
    astra::test::require(first && second && first->action == "move" && second->action == "move",
                         "both workers must receive reachable economic moves");
    astra::test::require(first->target_positions.front().x != second->target_positions.front().x ||
                             first->target_positions.front().y != second->target_positions.front().y,
                         "workers must reserve distinct next cells");
}

ASTRA_TEST(strategy_accepts_nearby_ready_task_and_stays_during_active_task) {
    auto turn = economy_turn();
    turn.team_our.roles.clear();
    turn.team_our.roles.push_back(pioneer(10011, {3, 3}));
    turn.team_our.player_tasks.push_back(
        {"自进化类1", {4, 3}, 0, 50, 30, true, std::nullopt});

    const auto ready = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(ready, 10011) &&
                             command_for(ready, 10011)->action == "acceptTask",
                         "pioneer beside ready own task point must accept it");

    turn.phase_task = "已领取的合成任务";
    const auto active = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(active, 10011) == nullptr,
                         "pioneer must preserve position while task is active");
}

ASTRA_TEST(strategy_returns_empty_for_unreachable_task_or_missing_night_station) {
    auto turn = economy_turn();
    turn.team_our.roles.clear();
    turn.team_our.roles.push_back(pioneer(10011, {1, 1}));
    turn.team_our.player_tasks.push_back(
        {"自进化类1", {4, 4}, 0, 50, 30, true, std::nullopt});
    for (int y = 0; y < turn.map.height; ++y) {
        turn.map.zones.push_back({{3, y}, "stone"});
    }
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "unreachable task point must not produce a guessed move");

    turn.round_no = 71;
    turn.map.zones.clear();
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "night return must be empty when station is not observed");
}

ASTRA_TEST(strategy_returns_all_characters_toward_station_at_night) {
    auto turn = economy_turn();
    turn.round_no = 71;
    turn.map.width = 15;
    turn.map.height = 15;
    turn.team_our.roles = {
        worker(10010, {1, 1}), worker(10012, {1, 5}), pioneer(10011, {5, 1}),
        station({10, 10}),
    };

    const auto decision = astra::BaselineStrategy().decide(turn);
    for (const int id : {10010, 10012, 10011}) {
        const auto* command = command_for(decision, id);
        astra::test::require(command && command->action == "move",
                             "each living character must receive a night return move");
    }
    const auto& first = command_for(decision, 10010)->target_positions.front();
    const auto& second = command_for(decision, 10012)->target_positions.front();
    const auto& scout = command_for(decision, 10011)->target_positions.front();
    std::set<std::pair<int, int>> destinations = {
        {first.x, first.y}, {second.x, second.y}, {scout.x, scout.y}};
    astra::test::require(destinations.size() == 3,
                         "night return moves must reserve distinct destinations");
}

ASTRA_TEST(strategy_starts_return_before_daylight_expires) {
    auto turn = economy_turn();
    turn.round_no = 70;
    turn.team_our.roles.front().pos = {1, 1};
    turn.team_our.roles.push_back(station({10, 8}));
    turn.map.zones.push_back({{2, 1}, "copper"});
    turn.map.zones.push_back({{10, 5}, "vendor"});

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* command = command_for(decision, 10010);
    astra::test::require(command && command->action == "move",
                         "worker must move toward station before daylight expires");
    astra::test::require(command->target_positions.front().x > 1 ||
                             command->target_positions.front().y > 1,
                         "return move must approach the observed station ring");
}

ASTRA_TEST(strategy_prioritizes_night_attack_and_returns_unused_characters) {
    auto turn = economy_turn();
    turn.round_no = 71;
    turn.map.width = 20;
    turn.map.height = 20;
    auto gun = station({5, 5});
    gun.id = 10020;
    gun.role_type = astra::RoleType::gatling;
    gun.role_type_raw = "gatling";
    gun.attack_power = 10;
    gun.attack_range = 5;
    gun.level = 1;
    gun.cooldown = 0;
    turn.team_our.roles = {
        worker(10010, {4, 5}), worker(10012, {1, 5}), pioneer(10011, {5, 1}),
        gun, station({10, 10}),
    };
    turn.robots.push_back({30001, {8, 5}, "smallRobot", 40, "", "challenger"});

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* attack = command_for(decision, 10020);
    astra::test::require(attack && attack->action == "attack" &&
                             attack->controller_id == "10010",
                         "adjacent character must control the ready night weapon");
    astra::test::require(command_for(decision, 10010) == nullptr,
                         "weapon controller must not also receive a return move");
    astra::test::require(command_for(decision, 10012) &&
                             command_for(decision, 10012)->action == "move",
                         "unused worker must continue returning to station");
}

ASTRA_TEST(strategy_runs_task_prompt_without_stopping_worker_economy) {
    auto turn = economy_turn();
    turn.map.zones.push_back({{1, 2}, "copper"});
    turn.map.zones.push_back({{10, 8}, "vendor"});
    turn.team_our.roles.push_back(pioneer(10011, {4, 4}));
    turn.phase_task = "读取本地数据并返回指定 JSON。";

    const auto first = astra::BaselineStrategy().decide(turn);
    astra::test::require(first.prompt && first.prompt->find(turn.phase_task) != std::string::npos,
                         "active task must produce a model prompt");
    astra::test::require(command_for(first, 10010) &&
                             command_for(first, 10010)->action == "collect",
                         "task prompt must not stop independent worker economy");
    astra::test::require(command_for(first, 10011) == nullptr,
                         "pioneer must remain at its task while waiting for the model");

    turn.llm_response = R"({"kind":"answer","answer":{"value":42}})";
    const auto answered = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(answered, 10011) &&
                             command_for(answered, 10011)->action == "submitAnswer" &&
                             command_for(answered, 10011)->task_answer == "{\"value\":42}",
                         "model answer must become the pioneer submitAnswer action");

    turn.round_no = 71;
    turn.team_our.roles.push_back(station({10, 8}));
    turn.llm_response = R"({"kind":"command","command":"python3 -c 'print(42)'"})";
    const auto command = astra::BaselineStrategy().decide(turn);
    astra::test::require(command.execute_command &&
                             command.execute_command->find("python3") == 0,
                         "night task command must reach the sandbox");
    astra::test::require(command_for(command, 10011) == nullptr,
                         "active-task pioneer must not leave the task point at night");
}

ASTRA_TEST(strategy_builds_distinct_far_rockets_before_worker_economy) {
    auto turn = economy_turn();
    turn.map.width = 15;
    turn.map.height = 15;
    turn.team_our.gold = 75;
    turn.team_our.roles = {
        worker(10010, {12, 10}), worker(10012, {10, 10}), station({10, 8}),
    };

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* first = command_for(decision, 10010);
    const auto* second = command_for(decision, 10012);
    astra::test::require(first && second && first->action == "build" &&
                             second->action == "build",
                         "two available workers must start two rockets in the same round");
    astra::test::require(first->name == std::optional<std::string>("rocket") &&
                             second->name == std::optional<std::string>("rocket"),
                         "opening weapon builds must both be rockets");
    const std::set<std::pair<int, int>> targets = {
        {first->target_positions.front().x, first->target_positions.front().y},
        {second->target_positions.front().x, second->target_positions.front().y},
    };
    astra::test::require(targets == std::set<std::pair<int, int>>{{11, 9}, {12, 9}},
                         "the two far rocket sites must be built before the near site");
}

ASTRA_TEST(strategy_moves_worker_off_its_assigned_rocket_site_before_building) {
    auto turn = economy_turn();
    turn.map.width = 15;
    turn.map.height = 15;
    turn.team_our.roles = {
        worker(10010, {12, 9}), worker(10012, {10, 10}), station({10, 8}),
    };

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* first = command_for(decision, 10010);
    const auto* second = command_for(decision, 10012);
    astra::test::require(first && first->action == "move",
                         "worker standing on a planned site must step aside first");
    astra::test::require(second && second->action == "build",
                         "other worker must still build its distinct rocket site");
}

ASTRA_TEST(strategy_buys_and_uses_upgrades_on_far_rockets_first) {
    auto turn = economy_turn();
    turn.map.width = 15;
    turn.map.height = 15;
    turn.team_our.gold = 100;
    turn.weapon_shop = {{"WeaponUpgradeVoucher1", 100},
                        {"WeaponUpgradeVoucher2", 150}};
    turn.map.zones.push_back({{8, 10}, "weaponShop"});
    turn.team_our.roles = {
        worker(10010, {9, 10}),
        station({10, 8}),
        rocket(10040, {12, 9}, 1),
        rocket(10041, {11, 9}, 1),
        rocket(10042, {9, 6}, 1),
    };

    const auto purchase = astra::BaselineStrategy().decide(turn);
    const auto* buy = command_for(purchase, 10010);
    astra::test::require(buy && buy->action == "buy" &&
                             buy->name == std::optional<std::string>("WeaponUpgradeVoucher1"),
                         "first upgrade purchase must be a level1 weapon voucher");

    turn.team_our.roles.front().pos = {12, 10};
    turn.team_our.roles.front().backpack = {"WeaponUpgradeVoucher1"};
    const auto use_first = astra::BaselineStrategy().decide(turn);
    const auto* use = command_for(use_first, 10010);
    astra::test::require(use && use->action == "use" &&
                             use->target_positions.front().x == 12 &&
                             use->target_positions.front().y == 9,
                         "first far rocket must receive the carried voucher");

    turn.team_our.roles[2].level = 2;
    turn.team_our.roles.front().pos = {10, 10};
    const auto use_second = astra::BaselineStrategy().decide(turn);
    use = command_for(use_second, 10010);
    astra::test::require(use && use->action == "use" &&
                             use->target_positions.front().x == 11 &&
                             use->target_positions.front().y == 9,
                         "second far rocket must reach level2 before the near rocket");

    turn.team_our.roles[3].level = 2;
    turn.team_our.roles.front().backpack.clear();
    turn.team_our.roles.front().pos = {9, 10};
    turn.team_our.gold = 150;
    const auto tier_two = astra::BaselineStrategy().decide(turn);
    buy = command_for(tier_two, 10010);
    astra::test::require(buy && buy->action == "buy" &&
                             buy->name == std::optional<std::string>("WeaponUpgradeVoucher2"),
                         "both far rockets must continue toward level3 before upgrading near");
}
