#include "defense.hpp"
#include "navigation.hpp"
#include "strategy.hpp"
#include "test_support.hpp"

#include "optional.hpp"
#include <algorithm>
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

astra::TurnObservation late_game_turn() {
    auto turn = economy_turn();
    turn.map.width = 41;
    turn.map.height = 32;
    turn.team_our.gold = 1300;
    turn.map.zones = {{{6, 28}, "weaponShop"}, {{6, 27}, "vendor"}};
    turn.team_our.roles = {worker(10010, {7, 28}), station({10, 24}),
                           rocket(10040, {9, 25}, 3), rocket(10041, {9, 22}, 3),
                           rocket(10042, {12, 22}, 3)};
    turn.team_our.roles[1].level = 3;
    turn.team_our.roles[1].health = 4500;
    const auto layout = astra::derive_defense_layout(turn);
    int id = 11000;
    for (const auto& pos : layout->front_wall_tiles) {
        auto wall = station(pos);
        wall.id = id++;
        wall.role_type = astra::RoleType::wall;
        wall.role_type_raw = "wall";
        wall.level = 3;
        turn.team_our.roles.push_back(wall);
    }
    auto enemy = station({29, 8});
    enemy.id = 20013;
    enemy.owned = false;
    turn.team_enemy.push_back(enemy);
    turn.weapon_shop = {{"WeaponUpgradeVoucher1", 100}, {"WeaponUpgradeVoucher2", 150},
                        {"WallUpgradeVoucher1", 20}, {"WallUpgradeVoucher2", 30},
                        {"StationUpgradeVoucher1", 100}, {"StationUpgradeVoucher2", 150},
                        {"LargeRobotSummonOrder", 100}, {"BossRobotSummonOrder", 200}};
    return turn;
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

ASTRA_TEST(strategy_resumes_both_workers_and_pioneer_after_own_wave_clears) {
    auto turn = economy_turn();
    turn.round_no = 90;
    turn.team_our.roles = {worker(10010, {1, 1}), worker(10012, {1, 3}),
                           pioneer(10011, {5, 3})};
    turn.map.zones = {{{2, 1}, "copper"}, {{2, 3}, "copper"}, {{10, 8}, "vendor"}};
    turn.team_our.player_tasks.push_back(
        {"task", {6, 3}, 0, 50, 30, true, astra::nullopt});
    turn.robots.push_back({30001, {11, 9}, "smallRobot", 40, "", "defender"});
    const auto decision = astra::BaselineStrategy().decide(turn);
    for (int id : {10010, 10012}) {
        const auto* command = command_for(decision, id);
        astra::test::require(command && command->action == "collect",
                             "both workers must resume collection after their wave clears");
    }
    const auto* command = command_for(decision, 10011);
    astra::test::require(command && command->action == "acceptTask",
                         "pioneer must resume tasks while enemy wave remains elsewhere");
    auto armed = turn;
    auto gun = rocket(10040, {0, 1}, 3);
    gun.attack_power = 20;
    armed.team_our.roles.push_back(gun);
    const auto working = astra::BaselineStrategy().decide(armed);
    astra::test::require(command_for(working, 10010) &&
                             command_for(working, 10010)->action == "collect" &&
                             command_for(working, 10040) == nullptr,
                         "enemy-wave scoring must not monopolize a worker after own wave clears");
    turn.robots.front().target_team = astra::nullopt;
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "unknown robot target must conservatively keep characters defending");
    turn.robots.front().target_team = "challenger";
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "living own-wave robots must keep characters defending");
    turn.robots.clear();
    turn.round_no = 71;
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "first night turn must wait for wave spawn visibility");
}

ASTRA_TEST(strategy_clear_night_can_start_mining_route_without_building) {
    auto turn = economy_turn();
    turn.round_no = 129;
    turn.team_our.roles.front().pos = {5, 1};
    turn.team_our.roles.push_back(station({9, 7}));
    turn.map.zones = {{{2, 1}, "copper"}, {{1, 1}, "vendor"}};
    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* command = command_for(decision, 10010);
    astra::test::require(command && command->action == "move" &&
                             command->target_positions.front().x == 4,
                         "clear night must budget through next day and move toward mine");
}

ASTRA_TEST(strategy_applies_clear_night_upgrade_purchase_return_and_use) {
    auto turn = late_game_turn();
    turn.round_no = 90;
    turn.team_our.roles[2].level = 1;
    bool bought = false;
    bool upgraded = false;
    for (; turn.round_no < 120 && !upgraded; ++turn.round_no) {
        const auto decision = astra::BaselineStrategy().decide(turn);
        const auto* command = command_for(decision, 10010);
        astra::test::require(command != nullptr, "upgrade trip must not stall after clearing wave");
        auto& actor = turn.team_our.roles.front();
        if (command->action == "buy") {
            astra::test::require(!bought && command->name == "WeaponUpgradeVoucher1",
                                 "upgrade trip must buy the expected voucher exactly once");
            actor.backpack.push_back(*command->name);
            turn.team_our.gold -= 100;
            bought = true;
        } else if (command->action == "move") {
            actor.pos = command->target_positions.front();
        } else if (command->action == "use") {
            astra::test::require(bought && !actor.backpack.empty() &&
                                     command->target_positions.front().x == 9 &&
                                     command->target_positions.front().y == 25,
                                 "carried voucher must reach first rear rocket");
            actor.backpack.clear();
            turn.team_our.roles[2].level = 2;
            upgraded = true;
        } else {
            astra::test::require(false, "upgrade worker must finish its purchase and delivery");
        }
    }
    astra::test::require(upgraded, "applied moves must deliver and use purchased upgrade before night ends");
}

ASTRA_TEST(strategy_upgrade_buyer_is_not_reassigned_to_mineral_carrier) {
    auto turn = late_game_turn();
    turn.team_our.roles[2].level = 1;
    turn.team_our.roles.push_back(worker(10012, {20, 20}));
    turn.team_our.roles.back().backpack = {"copper"};
    for (int round : {20, 90}) {
        turn.round_no = round;
        const auto decision = astra::BaselineStrategy().decide(turn);
        const auto* command = command_for(decision, 10010);
        astra::test::require(command && command->action == "buy" &&
                                 command->name == "WeaponUpgradeVoucher1",
                             "nearby empty worker must buy upgrade despite other worker carrying minerals");
    }
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
    astra::test::require(command->name == astra::Optional<std::string>("copper") &&
                             command->number == astra::Optional<int>(2),
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
        {"自进化类1", {4, 3}, 0, 50, 30, true, astra::nullopt});

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
        {"自进化类1", {4, 4}, 0, 50, 30, true, astra::nullopt});
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
        worker(10010, {12, 10}), worker(10012, {12, 5}), station({10, 8}),
    };

    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* first = command_for(decision, 10010);
    const auto* second = command_for(decision, 10012);
    astra::test::require(first && second && first->action == "build" &&
                             second->action == "build",
                         "two available workers must start two rockets in the same round");
    astra::test::require(first->name == astra::Optional<std::string>("rocket") &&
                             second->name == astra::Optional<std::string>("rocket"),
                         "opening weapon builds must both be rockets");
    const std::set<std::pair<int, int>> targets = {
        {first->target_positions.front().x, first->target_positions.front().y},
        {second->target_positions.front().x, second->target_positions.front().y},
    };
    astra::test::require(targets == std::set<std::pair<int, int>>{{12, 6}, {12, 9}},
                         "the two far rocket sites must be built before the near site");
}

ASTRA_TEST(strategy_moves_worker_off_its_assigned_rocket_site_before_building) {
    auto turn = economy_turn();
    turn.map.width = 15;
    turn.map.height = 15;
    turn.team_our.roles = {
        worker(10010, {12, 9}), worker(10012, {12, 5}), station({10, 8}),
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
        rocket(10041, {12, 6}, 1),
        rocket(10042, {9, 6}, 1),
    };

    const auto purchase = astra::BaselineStrategy().decide(turn);
    const auto* buy = command_for(purchase, 10010);
    astra::test::require(buy && buy->action == "buy" &&
                             buy->name == astra::Optional<std::string>("WeaponUpgradeVoucher1"),
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
    turn.team_our.roles.front().pos = {12, 5};
    const auto use_second = astra::BaselineStrategy().decide(turn);
    use = command_for(use_second, 10010);
    astra::test::require(use && use->action == "use" &&
                             use->target_positions.front().x == 12 &&
                             use->target_positions.front().y == 6,
                         "second far rocket must reach level2 before the near rocket");

    turn.team_our.roles[3].level = 2;
    turn.team_our.roles.front().backpack.clear();
    turn.team_our.roles.front().pos = {9, 10};
    turn.team_our.gold = 150;
    const auto tier_two = astra::BaselineStrategy().decide(turn);
    buy = command_for(tier_two, 10010);
    astra::test::require(buy && buy->action == "buy" &&
                             buy->name == astra::Optional<std::string>("WeaponUpgradeVoucher2"),
                         "both far rockets must continue toward level3 before upgrading near");
}

ASTRA_TEST(strategy_collects_stone_for_front_wall_while_other_worker_upgrades) {
    auto turn = economy_turn();
    turn.map.width = 15;
    turn.map.height = 15;
    turn.team_our.gold = 100;
    turn.weapon_shop = {{"WeaponUpgradeVoucher1", 100}};
    turn.map.zones = {{{6, 10}, "weaponShop"}, {{7, 5}, "stone"}};
    turn.team_our.roles = {
        worker(10010, {7, 10}), worker(10012, {7, 6}), station({10, 8}),
        rocket(10040, {12, 9}, 1), rocket(10041, {12, 6}, 1),
        rocket(10042, {9, 6}, 1),
    };

    const auto decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) &&
                             command_for(decision, 10010)->action == "buy",
                         "upgrade worker must keep buying the far-rocket voucher");
    astra::test::require(command_for(decision, 10012) &&
                             command_for(decision, 10012)->action == "collect" &&
                             command_for(decision, 10012)->target_positions.front().x == 7 &&
                             command_for(decision, 10012)->target_positions.front().y == 5,
                         "wall worker must collect one stone per round for the half-wall");
}

ASTRA_TEST(strategy_builds_only_the_front_half_wall_with_reserved_stone) {
    auto turn = economy_turn();
    turn.map.width = 15;
    turn.map.height = 15;
    auto builder = worker(10012, {7, 5});
    builder.backpack.assign(10, "stone");
    turn.team_our.roles = {
        worker(10010, {1, 1}), builder, station({10, 8}),
        rocket(10040, {12, 9}, 3), rocket(10041, {12, 6}, 3),
        rocket(10042, {9, 6}, 3),
    };

    const auto layout = astra::derive_defense_layout(turn);
    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* build = command_for(decision, 10012);
    astra::test::require(layout && build && build->action == "build" &&
                             build->name == astra::Optional<std::string>("wall"),
                         "prepared wall worker must build a wall");
    astra::test::require(std::any_of(layout->front_wall_tiles.begin(),
                                    layout->front_wall_tiles.end(),
                                    [&](const astra::Pos& pos) {
                                        return pos.x == build->target_positions.front().x &&
                                               pos.y == build->target_positions.front().y;
                                    }),
                         "wall target must belong to the selected front half only");
}

ASTRA_TEST(strategy_rebuild_shortage_does_not_block_sale_or_other_worker) {
    auto turn = economy_turn();
    turn.map.width = 41;
    turn.map.height = 32;
    turn.team_our.gold = 0;
    turn.map.zones = {{{7, 24}, "vendor"}, {{12, 27}, "copper"}};
    turn.team_our.roles = {worker(10010, {8, 25}), worker(10012, {11, 26}),
                           station({10, 24}), rocket(10042, {12, 22}, 1)};
    turn.team_our.roles.front().backpack.assign(10, "copper");
    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* sale = command_for(decision, 10010);
    const auto* collect = command_for(decision, 10012);
    astra::test::require(sale && sale->action == "sell" && sale->number == 10,
                         "rebuild shortage must allow a carried mineral sale");
    astra::test::require(collect && collect->action == "collect",
                         "unfunded second builder must return to mining");

    turn.team_our.gold = 25;
    turn.team_our.roles.front().backpack.clear();
    const auto one_build = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(one_build, 10010) &&
                             command_for(one_build, 10010)->action == "build",
                         "one affordable rocket must still be built");
    astra::test::require(command_for(one_build, 10012) &&
                             command_for(one_build, 10012)->action == "collect",
                         "second worker must not lose its turn overspending shared gold");
}

ASTRA_TEST(strategy_returns_to_staff_all_three_rockets_over_applied_rounds) {
    auto turn = economy_turn();
    turn.map.width = 41;
    turn.map.height = 32;
    turn.round_no = 71;
    turn.team_our.roles = {worker(10010, {10, 22}), worker(10012, {11, 22}),
                           pioneer(10011, {12, 23}), station({10, 24}),
                           rocket(10040, {9, 25}, 3), rocket(10041, {9, 22}, 3),
                           rocket(10042, {12, 22}, 3)};
    for (auto& role : turn.team_our.roles) {
        if (role.role_type == astra::RoleType::rocket) role.attack_power = 20;
    }
    turn.robots = {{30001, {18, 18}, "bossRobot", 2000, "", "challenger"}};
    std::set<int> fired;
    for (int step = 0; step < 12; ++step, ++turn.round_no) {
        const auto decision = astra::BaselineStrategy().decide(turn);
        std::set<std::pair<int, int>> destinations;
        for (auto& role : turn.team_our.roles) {
            const auto* command = command_for(decision, role.id);
            if (role.role_type == astra::RoleType::rocket && role.cooldown && *role.cooldown > 0) {
                --*role.cooldown;
            }
            if (!command) continue;
            if (command->action == "attack") {
                fired.insert(role.id);
                role.cooldown = 3;
                astra::test::require(command_for(decision, std::stoi(*command->controller_id)) == nullptr,
                                     "a firing controller must not move simultaneously");
            } else if (command->action == "move") {
                const auto next = command->target_positions.front();
                astra::test::require(destinations.emplace(next.x, next.y).second,
                                     "returning actors must not collide");
                role.pos = next;
            }
        }
    }
    astra::test::require(fired == std::set<int>({10040, 10041, 10042}),
                         "both rear rockets must gain operators instead of idling at station ring");
}

ASTRA_TEST(strategy_accepts_task_from_second_occupied_cell_for_both_sides) {
    for (const std::string side : {"challenger", "defender"}) {
        auto turn = economy_turn();
        turn.map.width = 41;
        turn.map.height = 32;
        turn.team_our.type = side;
        turn.team_our.roles = {pioneer(10011, {15, 17})};
        turn.team_our.player_tasks = {{"selfEvolution", {17, 17}, 0, 50, 30, true, 20}};
        turn.map.zones = {{{17, 17}, side + "TaskPoint2"},
                          {{16, 17}, side + "TaskPoint2"},
                          {{14, 17}, side + "TaskPoint1"}};
        const auto decision = astra::BaselineStrategy().decide(turn);
        const auto* command = command_for(decision, 10011);
        astra::test::require(command && command->action == "acceptTask",
                             "own task point second cell must allow immediate acceptance");
        turn.team_our.player_tasks.front().cooldown_rounds = 1;
        astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                             "second-cell geometry must not bypass task cooldown");
    }
}

ASTRA_TEST(strategy_leaves_station_ring_for_rocket_post_before_dusk) {
    auto turn = economy_turn();
    turn.round_no = 70;
    turn.map.width = 41;
    turn.map.height = 32;
    turn.team_our.roles = {worker(10010, {10, 22}), station({10, 24}),
                           rocket(10040, {9, 25}, 3)};
    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* move = command_for(decision, 10010);
    astra::test::require(move && move->action == "move",
                         "being on station ring must not prevent moving to a rocket before dusk");
}

ASTRA_TEST(strategy_upgrades_weapons_then_front_walls_then_station) {
    auto turn = late_game_turn();
    turn.team_our.roles[4].level = 2;
    turn.team_our.roles[5].level = 1;
    turn.team_our.roles[1].level = 1;
    auto decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) &&
                             command_for(decision, 10010)->name == "WeaponUpgradeVoucher2",
                         "unfinished near rocket must still precede walls and station");

    turn.team_our.roles[4].level = 3;
    turn.team_our.gold = 20;
    decision = astra::BaselineStrategy().decide(turn);
    const auto* buy = command_for(decision, 10010);
    astra::test::require(buy && buy->action == "buy" && buy->name == "WallUpgradeVoucher1",
                         "finished weapons must unlock the pressure-facing wall upgrade");

    const auto target = turn.team_our.roles[5].pos;
    turn.team_our.roles.front().pos = {target.x + 1, target.y};
    turn.team_our.roles.front().backpack = {"WallUpgradeVoucher1"};
    turn.team_our.gold = 0;
    decision = astra::BaselineStrategy().decide(turn);
    const auto* use = command_for(decision, 10010);
    astra::test::require(use && use->action == "use" && use->name == "WallUpgradeVoucher1" &&
                             use->target_positions.front().x == target.x &&
                             use->target_positions.front().y == target.y,
                         "front-most wall must receive the carried voucher");

    turn.team_our.roles[5].level = 2;
    turn.team_our.roles[6].level = 1;
    turn.team_our.roles.front().backpack = {"WallUpgradeVoucher2", "StationUpgradeVoucher1"};
    decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) &&
                             command_for(decision, 10010)->name == "WallUpgradeVoucher2",
                         "front-most wall must reach level3 before side walls or station");

    for (auto& role : turn.team_our.roles) {
        if (role.role_type == astra::RoleType::wall) role.level = 3;
    }
    turn.team_our.roles.front().pos = {9, 24};
    turn.team_our.roles.front().backpack = {"StationUpgradeVoucher1"};
    decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) &&
                             command_for(decision, 10010)->action == "use" &&
                             command_for(decision, 10010)->name == "StationUpgradeVoucher1",
                         "finished front walls must unlock station upgrade");
    turn.team_our.roles[1].level = 2;
    turn.team_our.roles.front().backpack = {"StationUpgradeVoucher2"};
    decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) &&
                             command_for(decision, 10010)->name == "StationUpgradeVoucher2",
                         "station must be upgraded through level3");
}

ASTRA_TEST(strategy_late_game_uses_surplus_for_large_boss_mix) {
    auto turn = late_game_turn();
    auto decision = astra::BaselineStrategy().decide(turn);
    const auto* buy = command_for(decision, 10010);
    astra::test::require(buy && buy->action == "buy" && buy->name == "LargeRobotSummonOrder" &&
                             buy->number == 10,
                         "1000 surplus should buy ten large orders, retaining 300 gold");
    turn.team_our.gold = 2300;
    decision = astra::BaselineStrategy().decide(turn);
    buy = command_for(decision, 10010);
    astra::test::require(buy && buy->name == "BossRobotSummonOrder" && buy->number == 10,
                         "2000 surplus should fill ten daily slots with bosses");
    turn.team_our.gold = 1400;
    decision = astra::BaselineStrategy().decide(turn);
    buy = command_for(decision, 10010);
    astra::test::require(buy && buy->name == "BossRobotSummonOrder" && buy->number == 1,
                         "1100 surplus should start the nine-large/one-boss mix with one boss");
}

ASTRA_TEST(strategy_summon_orders_require_full_defense_and_living_enemy) {
    for (const int incomplete : {1, 4, 5}) {
        auto turn = late_game_turn();
        turn.team_our.roles[incomplete].level = 2;
        const auto decision = astra::BaselineStrategy().decide(turn);
        const auto* command = command_for(decision, 10010);
        astra::test::require(!command || !command->name ||
                                 command->name->find("SummonOrder") == std::string::npos,
                             "any unfinished defense must preempt summon spending");
    }
    auto turn = late_game_turn();
    turn.team_our.roles.front().backpack = {"BossRobotSummonOrder"};
    turn.team_our.gold = 300;
    auto decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) &&
                             command_for(decision, 10010)->action == "use" &&
                             command_for(decision, 10010)->name == "BossRobotSummonOrder",
                         "use held orders before buying, even with no spendable gold");
    turn.team_enemy.front().health = 0;
    decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) == nullptr,
                         "do not summon for an already destroyed enemy base");
}

ASTRA_TEST(strategy_summon_purchases_respect_daylight_capacity_and_prices) {
    auto turn = late_game_turn();
    turn.team_our.roles.front().backpack_capacity = 2;
    auto decision = astra::BaselineStrategy().decide(turn);
    const auto* buy = command_for(decision, 10010);
    astra::test::require(buy && buy->action == "buy" && buy->number == 2,
                         "summon batch must fit free inventory slots");
    turn.team_our.roles.front().backpack_capacity = 100;
    turn.team_our.gold = 2300;
    turn.round_no = 65;
    decision = astra::BaselineStrategy().decide(turn);
    buy = command_for(decision, 10010);
    astra::test::require(!buy || buy->action != "buy" || buy->number.value_or(0) < 10,
                         "late-day purchase must leave time to use orders and return");
    turn.round_no = 1241;
    decision = astra::BaselineStrategy().decide(turn);
    buy = command_for(decision, 10010);
    astra::test::require(!buy || (buy->action != "buy" && buy->action != "use"),
                         "do not send orders after the final night has already spawned");
    turn.round_no = 10;
    turn.team_our.gold = 300;
    decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10010) == nullptr,
                         "the reconstruction reserve must not be spent on summons");
    turn.team_our.gold = 1300;
    turn.weapon_shop = {{"LargeRobotSummonOrder", 200}, {"BossRobotSummonOrder", 100}};
    decision = astra::BaselineStrategy().decide(turn);
    buy = command_for(decision, 10010);
    astra::test::require(buy && buy->name == "BossRobotSummonOrder" && buy->number == 10,
                         "summon allocation must use observed shop prices");
}

ASTRA_TEST(strategy_summon_batch_accounts_for_used_slots_and_other_worker_inventory) {
    auto turn = late_game_turn();
    turn.summon_orders_used = 9;
    auto decision = astra::BaselineStrategy().decide(turn);
    const auto* buy = command_for(decision, 10010);
    astra::test::require(buy && buy->action == "buy" && buy->number == 1,
                         "only one order may be bought when nine daily slots are already used");
    turn.summon_orders_used = 10;
    astra::test::require(astra::BaselineStrategy().decide(turn).role_commands.empty(),
                         "daily cap must stop both purchasing and consumption");

    turn.summon_orders_used = 0;
    auto other = worker(10012, {7, 27});
    other.backpack.assign(10, "LargeRobotSummonOrder");
    turn.team_our.roles.push_back(other);
    decision = astra::BaselineStrategy().decide(turn);
    astra::test::require(command_for(decision, 10012) &&
                             command_for(decision, 10012)->action == "use" &&
                             command_for(decision, 10010) == nullptr,
                         "consume the other worker's inventory before buying a redundant batch");
}

ASTRA_TEST(strategy_returns_to_rebuilding_when_a_max_level_defense_is_destroyed) {
    auto turn = late_game_turn();
    turn.team_our.roles[2].health = 0;
    turn.team_our.roles.front().backpack = {"BossRobotSummonOrder"};
    const auto decision = astra::BaselineStrategy().decide(turn);
    const auto* command = command_for(decision, 10010);
    astra::test::require(command && (command->action == "move" || command->action == "build"),
                         "destroyed level3 rocket must preempt held summons and trigger rebuilding");
}

ASTRA_TEST(strategy_applies_a_complete_mixed_summon_purchase_and_use_sequence) {
    auto turn = late_game_turn();
    turn.team_our.gold = 1400;
    int bosses = 0;
    int large = 0;
    for (int step = 0; step < 15; ++step, ++turn.round_no) {
        const auto decision = astra::BaselineStrategy().decide(turn);
        const auto* command = command_for(decision, 10010);
        if (!command) continue;
        astra::test::require(command->name && astra::is_robot_summon_order(*command->name),
                             "fully funded late-game sequence should only trade summon orders");
        auto& inventory = turn.team_our.roles.front().backpack;
        if (command->action == "buy") {
            const int quantity = command->number.value_or(1);
            const int price = *command->name == "BossRobotSummonOrder" ? 200 : 100;
            turn.team_our.gold -= quantity * price;
            inventory.insert(inventory.end(), quantity, *command->name);
        } else if (command->action == "use") {
            const auto item = std::find(inventory.begin(), inventory.end(), *command->name);
            astra::test::require(item != inventory.end(), "cannot use an unowned order");
            inventory.erase(item);
            ++turn.summon_orders_used;
            if (*command->name == "BossRobotSummonOrder") ++bosses;
            else ++large;
        } else {
            astra::test::require(false, "unexpected action in applied summon sequence");
        }
        astra::test::require(turn.team_our.gold >= 300 && turn.summon_orders_used <= 10,
                             "applied actions must preserve reconstruction money and daily cap");
    }
    astra::test::require(bosses == 1 && large == 9 && turn.team_our.gold == 300 &&
                             turn.team_our.roles.front().backpack.empty(),
                         "1100 surplus must actually turn into one boss and nine large summons");
}

ASTRA_TEST(strategy_mixed_summon_plan_counts_each_purchase_before_dusk) {
    auto turn = late_game_turn();
    turn.round_no = 60;
    turn.team_our.gold = 1000;
    int added_health = 0;
    for (; turn.round_no <= 70; ++turn.round_no) {
        const auto decision = astra::BaselineStrategy().decide(turn);
        const auto* command = command_for(decision, 10010);
        if (!command) continue;
        auto& actor = turn.team_our.roles.front();
        if (command->action == "move") {
            actor.pos = command->target_positions.front();
        } else if (command->action == "buy") {
            const int quantity = command->number.value_or(1);
            turn.team_our.gold -= quantity * (*command->name == "BossRobotSummonOrder" ? 200 : 100);
            actor.backpack.insert(actor.backpack.end(), quantity, *command->name);
        } else if (command->action == "use") {
            const auto item = std::find(actor.backpack.begin(), actor.backpack.end(), *command->name);
            astra::test::require(item != actor.backpack.end(), "summon must be in inventory");
            actor.backpack.erase(item);
            added_health += *command->name == "BossRobotSummonOrder" ? 800 : 500;
            ++turn.summon_orders_used;
        }
    }
    astra::test::require(added_health >= 3000 && turn.team_our.gold >= 300 &&
                             turn.team_our.roles.front().backpack.empty(),
                         "mixed purchases must fit before dusk and outperform six feasible large orders");
}
