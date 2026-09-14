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

astra::TurnObservation economy_turn() {
    astra::TurnObservation turn;
    turn.round_no = 10;
    turn.map.width = 12;
    turn.map.height = 10;
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
