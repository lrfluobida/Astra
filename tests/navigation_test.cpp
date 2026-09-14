#include "navigation.hpp"
#include "test_support.hpp"

#include <algorithm>

namespace {

astra::UnitObservation actor_at(int id, astra::Pos pos) {
    astra::UnitObservation actor;
    actor.id = id;
    actor.pos = pos;
    actor.role_type = astra::RoleType::worker;
    actor.role_type_raw = "worker";
    actor.health = 100;
    actor.owned = true;
    return actor;
}

astra::TurnObservation empty_turn(int width, int height, astra::Pos start) {
    astra::TurnObservation turn;
    turn.round_no = 1;
    turn.map.width = width;
    turn.map.height = height;
    turn.team_our.roles.push_back(actor_at(1, start));
    return turn;
}

bool has_pos(const std::vector<astra::Pos>& positions, astra::Pos expected) {
    return std::any_of(positions.begin(), positions.end(), [&](const astra::Pos& pos) {
        return pos.x == expected.x && pos.y == expected.y;
    });
}

}  // namespace

ASTRA_TEST(navigation_routes_around_visible_occupancy) {
    auto turn = empty_turn(5, 5, {0, 2});
    turn.map.zones.push_back({{1, 2}, "stone"});
    turn.robots.push_back({9, {1, 1}, "smallRobot", 40, "", std::nullopt});
    turn.map.zones.push_back({{1, 3}, "iron"});

    const auto step = astra::next_step_toward_any(turn, 1, {{4, 2}}, {});

    astra::test::require(step.has_value(), "goal must remain reachable around obstacles");
    astra::test::require(!(step->next.x == 1 && step->next.y == 2),
                         "next step must not enter a neutral zone");
    astra::test::require(!(step->next.x == 1 && step->next.y == 1),
                         "next step must not enter a robot cell");
    astra::test::require(step->distance > 4,
                         "reported distance must include the obstacle detour");
}

ASTRA_TEST(navigation_allows_diagonal_between_adjacent_obstacles) {
    auto turn = empty_turn(3, 3, {0, 0});
    turn.map.zones.push_back({{1, 0}, "stone"});
    turn.map.zones.push_back({{0, 1}, "iron"});

    const auto step = astra::next_step_toward_any(turn, 1, {{1, 1}}, {});

    astra::test::require(step.has_value() && step->next.x == 1 && step->next.y == 1,
                         "diagonal gap must remain traversable");
    astra::test::require(step->distance == 1, "diagonal movement costs one round");
}

ASTRA_TEST(navigation_expands_station_footprint_and_interaction_ring) {
    astra::UnitObservation station;
    station.id = 13;
    station.pos = {2, 2};
    station.role_type = astra::RoleType::station;
    station.role_type_raw = "station";
    station.health = 1500;
    station.owned = true;

    const auto footprint = astra::occupied_cells(station);
    astra::test::require(footprint.size() == 4, "station must occupy four cells");
    astra::test::require(has_pos(footprint, {2, 2}) && has_pos(footprint, {3, 2}) &&
                             has_pos(footprint, {2, 3}) && has_pos(footprint, {3, 3}),
                         "station pos must be expanded from its top-left coordinate");

    auto turn = empty_turn(6, 6, {0, 0});
    turn.team_our.roles.push_back(station);
    const auto goals = astra::station_interaction_cells(turn);
    astra::test::require(goals.size() == 12, "interior 2x2 station must have 12 ring cells");
    for (const auto& cell : footprint) {
        astra::test::require(!has_pos(goals, cell), "station footprint cannot be a return goal");
    }

    turn.team_our.roles.erase(turn.team_our.roles.begin() + 1);
    astra::test::require(astra::station_interaction_cells(turn).empty(),
                         "missing station must not produce guessed return goals");
}

ASTRA_TEST(navigation_returns_empty_when_goal_is_unreachable) {
    auto turn = empty_turn(3, 3, {1, 1});
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (x != 1 || y != 1) turn.map.zones.push_back({{x, y}, "stone"});
        }
    }
    astra::test::require(!astra::next_step_toward_any(turn, 1, {{0, 0}}, {}).has_value(),
                         "fully enclosed actor must report unreachable");
}

ASTRA_TEST(navigation_reserves_next_cells_and_prevents_position_exchange) {
    const auto turn = empty_turn(4, 3, {0, 1});
    astra::NavigationReservations reserved_destination;
    reserved_destination.destinations.push_back({1, 1});
    const auto detour =
        astra::next_step_toward_any(turn, 1, {{3, 1}}, reserved_destination);
    astra::test::require(detour.has_value() &&
                             !(detour->next.x == 1 && detour->next.y == 1),
                         "later actor must not use an already reserved next cell");

    astra::NavigationReservations exchange;
    exchange.edges.push_back({{1, 1}, {0, 1}});
    const auto no_swap = astra::next_step_toward_any(turn, 1, {{1, 1}}, exchange);
    astra::test::require(no_swap.has_value() &&
                             !(no_swap->next.x == 1 && no_swap->next.y == 1),
                         "actor must not use a reserved reverse edge as its next step");
}

ASTRA_TEST(navigation_builds_interaction_cells_around_neutral_target) {
    const auto turn = empty_turn(4, 4, {0, 0});
    const auto cells = astra::interaction_cells(turn, {2, 2});
    astra::test::require(cells.size() == 8, "interior neutral target must have eight neighbors");
    astra::test::require(!has_pos(cells, {2, 2}), "neutral target itself is not standable");
}
