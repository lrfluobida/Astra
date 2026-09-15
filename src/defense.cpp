#include "defense.hpp"

namespace astra {
namespace {

bool in_bounds(const TurnObservation& turn, const Pos& pos) {
    return pos.x >= 0 && pos.y >= 0 && pos.x < turn.map.width && pos.y < turn.map.height;
}

const UnitObservation* find_station(const TurnObservation& turn) {
    for (const auto& unit : turn.team_our.roles) {
        if (unit.role_type == RoleType::station) return &unit;
    }
    return nullptr;
}

void append_ring(const TurnObservation& turn,
                 int left,
                 int right,
                 int bottom,
                 int top,
                 std::vector<Pos>& result) {
    for (int y = bottom; y <= top; ++y) {
        for (int x = left; x <= right; ++x) {
            if (x != left && x != right && y != bottom && y != top) continue;
            const Pos cell{x, y};
            if (in_bounds(turn, cell)) result.push_back(cell);
        }
    }
}

int direction(int delta) {
    return delta < 0 ? -1 : 1;
}

}  // namespace

std::optional<DefenseLayout> derive_defense_layout(const TurnObservation& turn) {
    const auto* station = find_station(turn);
    if (!station || turn.map.width <= 0 || turn.map.height <= 0) return std::nullopt;

    DefenseLayout layout;
    append_ring(turn,
                station->pos.x - 1,
                station->pos.x + 2,
                station->pos.y - 2,
                station->pos.y + 1,
                layout.weapon_build_tiles);
    append_ring(turn,
                station->pos.x - 2,
                station->pos.x + 3,
                station->pos.y - 3,
                station->pos.y + 2,
                layout.wall_build_tiles);

    const int toward_center_x = direction((turn.map.width - 1) - (2 * station->pos.x + 1));
    const int toward_center_y = direction((turn.map.height - 1) - (2 * station->pos.y - 1));
    const int near_x = toward_center_x > 0 ? station->pos.x + 2 : station->pos.x - 1;
    const int near_y = toward_center_y > 0 ? station->pos.y + 1 : station->pos.y - 2;
    const int far_x = toward_center_x > 0 ? station->pos.x - 1 : station->pos.x + 2;
    const int far_y = toward_center_y > 0 ? station->pos.y - 2 : station->pos.y + 1;
    layout.near_rocket = {near_x, near_y};
    layout.far_rockets = {{{far_x, far_y}, {far_x + toward_center_x, far_y}}};

    if (!in_bounds(turn, layout.near_rocket) || !in_bounds(turn, layout.far_rockets[0]) ||
        !in_bounds(turn, layout.far_rockets[1])) {
        return std::nullopt;
    }
    return layout;
}

}  // namespace astra
