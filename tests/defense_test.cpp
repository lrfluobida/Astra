#include "defense.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <set>

namespace {

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

astra::TurnObservation turn_with_station(astra::Pos pos) {
    astra::TurnObservation turn;
    turn.round_no = 1;
    turn.map.width = 41;
    turn.map.height = 32;
    turn.team_our.roles.push_back(station(pos));
    return turn;
}

bool has_pos(const std::vector<astra::Pos>& positions, astra::Pos expected) {
    return std::any_of(positions.begin(), positions.end(), [&](const astra::Pos& pos) {
        return pos.x == expected.x && pos.y == expected.y;
    });
}

std::set<std::pair<int, int>> pos_set(const std::vector<astra::Pos>& positions) {
    std::set<std::pair<int, int>> result;
    for (const auto& pos : positions) result.insert({pos.x, pos.y});
    return result;
}

astra::Pos mirror(astra::Pos pos) {
    return {40 - pos.x, 31 - pos.y};
}

}  // namespace

ASTRA_TEST(defense_derives_build_rings_and_three_rocket_layout) {
    const auto layout = astra::derive_defense_layout(turn_with_station({2, 29}));

    astra::test::require(layout.has_value(), "observed station must produce a defense layout");
    astra::test::require(layout->weapon_build_tiles.size() == 12,
                         "interior 2x2 station must have a 12-cell weapon ring");
    astra::test::require(layout->wall_build_tiles.size() == 20,
                         "weapon ring must have a 20-cell outer wall ring");
    astra::test::require(layout->front_wall_tiles.size() == 10,
                         "straight-line robots only require half of the wall ring");
    const std::vector<astra::Pos> expected_front_walls = {
        {5, 28}, {5, 29}, {5, 27}, {5, 30}, {5, 26},
        {5, 31}, {4, 26}, {3, 26}, {4, 31}, {3, 31},
    };
    astra::test::require(pos_set(layout->front_wall_tiles) == pos_set(expected_front_walls),
                         "front walls must form the exact connected U facing the enemy");
    for (std::size_t index = 0; index < expected_front_walls.size(); ++index) {
        astra::test::require(
            layout->front_wall_tiles[index].x == expected_front_walls[index].x &&
                layout->front_wall_tiles[index].y == expected_front_walls[index].y,
            "front wall upgrades must cover the center-first face before the two-cell wings");
    }
    const auto front_count = std::count_if(
        layout->front_wall_tiles.begin(), layout->front_wall_tiles.end(),
        [](const astra::Pos& pos) { return pos.x == 5 && pos.y >= 26 && pos.y <= 31; });
    const auto bottom_wing_count = std::count_if(
        layout->front_wall_tiles.begin(), layout->front_wall_tiles.end(),
        [](const astra::Pos& pos) { return pos.y == 26 && pos.x >= 3 && pos.x <= 4; });
    const auto top_wing_count = std::count_if(
        layout->front_wall_tiles.begin(), layout->front_wall_tiles.end(),
        [](const astra::Pos& pos) { return pos.y == 31 && pos.x >= 3 && pos.x <= 4; });
    astra::test::require(front_count == 6 && bottom_wing_count == 2 && top_wing_count == 2,
                         "front wall must contain a six-cell face and two two-cell wings");
    for (const auto& wall : layout->front_wall_tiles) {
        astra::test::require(has_pos(layout->wall_build_tiles, wall),
                             "every front wall must remain inside the legal wall ring");
    }
    astra::test::require(has_pos(layout->front_wall_tiles, {5, 26}) &&
                             has_pos(layout->front_wall_tiles, {5, 31}) &&
                             has_pos(layout->front_wall_tiles, {3, 26}),
                         "upper-left base must wall its right and bottom approach sides");
    astra::test::require(!has_pos(layout->front_wall_tiles, {0, 31}),
                         "rear corner must remain open");
    astra::test::require(has_pos(layout->weapon_build_tiles, layout->near_rocket),
                         "near rocket must be inside the weapon ring");
    astra::test::require(has_pos(layout->weapon_build_tiles, layout->far_rockets[0]) &&
                             has_pos(layout->weapon_build_tiles, layout->far_rockets[1]),
                         "far rockets must be inside the weapon ring");
    astra::test::require(layout->far_rockets[0].x == 1 && layout->far_rockets[0].y == 30 &&
                             layout->far_rockets[1].x == 1 && layout->far_rockets[1].y == 27,
                         "far rockets must share the rear edge across the upper and lower slots");
    const std::set<std::pair<int, int>> rockets = {
        {layout->near_rocket.x, layout->near_rocket.y},
        {layout->far_rockets[0].x, layout->far_rockets[0].y},
        {layout->far_rockets[1].x, layout->far_rockets[1].y},
    };
    astra::test::require(rockets.size() == 3, "rocket sites must be distinct");
}

ASTRA_TEST(defense_layout_mirrors_between_match_halves) {
    const auto upper_left = astra::derive_defense_layout(turn_with_station({2, 29}));
    const auto lower_right = astra::derive_defense_layout(turn_with_station({37, 3}));

    astra::test::require(upper_left.has_value() && lower_right.has_value(),
                         "both assigned base positions must produce layouts");
    astra::test::require(lower_right->near_rocket.x == mirror(upper_left->near_rocket).x &&
                             lower_right->near_rocket.y == mirror(upper_left->near_rocket).y,
                         "near rocket must mirror exactly after swapping positions");
    for (std::size_t index = 0; index < 2; ++index) {
        const auto expected = mirror(upper_left->far_rockets[index]);
        astra::test::require(lower_right->far_rockets[index].x == expected.x &&
                                 lower_right->far_rockets[index].y == expected.y,
                             "far rocket order must mirror exactly after swapping positions");
    }
    for (std::size_t index = 0; index < upper_left->front_wall_tiles.size(); ++index) {
        const auto expected = mirror(upper_left->front_wall_tiles[index]);
        astra::test::require(lower_right->front_wall_tiles[index].x == expected.x &&
                                 lower_right->front_wall_tiles[index].y == expected.y,
                             "front half-wall order must mirror exactly after swapping positions");
    }
}
