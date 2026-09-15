#include "navigation.hpp"

#include <algorithm>
#include <array>
#include <queue>

namespace astra {
namespace {

bool same_pos(const Pos& left, const Pos& right) {
    return left.x == right.x && left.y == right.y;
}

bool in_bounds(const TurnObservation& turn, const Pos& pos) {
    return pos.x >= 0 && pos.y >= 0 && pos.x < turn.map.width && pos.y < turn.map.height;
}

int index_of(const TurnObservation& turn, const Pos& pos) {
    return pos.y * turn.map.width + pos.x;
}

const UnitObservation* find_actor(const TurnObservation& turn, int actor_id) {
    for (const auto& unit : turn.team_our.roles) {
        if (unit.id == actor_id) return &unit;
    }
    return nullptr;
}

void block(std::vector<bool>& blocked, const TurnObservation& turn, const Pos& pos) {
    if (in_bounds(turn, pos)) blocked[index_of(turn, pos)] = true;
}

std::vector<bool> observed_occupancy(const TurnObservation& turn) {
    const int cell_count = turn.map.width * turn.map.height;
    std::vector<bool> blocked(static_cast<std::size_t>(std::max(0, cell_count)), false);
    for (const auto& zone : turn.map.zones) block(blocked, turn, zone.pos);
    for (const auto& unit : turn.team_our.roles) {
        for (const auto& cell : occupied_cells(unit)) block(blocked, turn, cell);
    }
    for (const auto& unit : turn.team_enemy) {
        for (const auto& cell : occupied_cells(unit)) block(blocked, turn, cell);
    }
    for (const auto& robot : turn.robots) block(blocked, turn, robot.pos);
    return blocked;
}

bool reverses_reserved_edge(const NavigationReservations& reservations,
                            const Pos& from,
                            const Pos& to) {
    return std::any_of(reservations.edges.begin(), reservations.edges.end(),
                       [&](const MovementEdge& edge) {
                           return same_pos(edge.from, to) && same_pos(edge.to, from);
                       });
}

}  // namespace

std::vector<Pos> occupied_cells(const UnitObservation& unit) {
    if (unit.role_type == RoleType::station) {
        return {{unit.pos.x, unit.pos.y},
                {unit.pos.x + 1, unit.pos.y},
                {unit.pos.x, unit.pos.y - 1},
                {unit.pos.x + 1, unit.pos.y - 1}};
    }
    return {unit.pos};
}

std::vector<Pos> interaction_cells(const TurnObservation& turn, Pos target) {
    std::vector<Pos> result;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Pos candidate{target.x + dx, target.y + dy};
            if (in_bounds(turn, candidate)) result.push_back(candidate);
        }
    }
    return result;
}

std::vector<Pos> station_interaction_cells(const TurnObservation& turn) {
    const UnitObservation* station = nullptr;
    for (const auto& unit : turn.team_our.roles) {
        if (unit.role_type == RoleType::station) {
            station = &unit;
            break;
        }
    }
    if (!station) return {};

    std::vector<Pos> result;
    for (int y = station->pos.y - 2; y <= station->pos.y + 1; ++y) {
        for (int x = station->pos.x - 1; x <= station->pos.x + 2; ++x) {
            const Pos candidate{x, y};
            const bool inside_station = x >= station->pos.x && x <= station->pos.x + 1 &&
                                        y >= station->pos.y - 1 && y <= station->pos.y;
            if (!inside_station && in_bounds(turn, candidate)) result.push_back(candidate);
        }
    }
    return result;
}

std::optional<PathStep> next_step_toward_any(const TurnObservation& turn,
                                             int actor_id,
                                             const std::vector<Pos>& goals,
                                             const NavigationReservations& reservations) {
    const UnitObservation* actor = find_actor(turn, actor_id);
    if (!actor || turn.map.width <= 0 || turn.map.height <= 0 || goals.empty()) {
        return std::nullopt;
    }

    auto blocked = observed_occupancy(turn);
    for (const auto& destination : reservations.destinations) block(blocked, turn, destination);
    if (!in_bounds(turn, actor->pos)) return std::nullopt;
    const int start = index_of(turn, actor->pos);
    blocked[start] = false;

    std::vector<bool> is_goal(blocked.size(), false);
    for (const auto& goal : goals) {
        if (in_bounds(turn, goal) && (!blocked[index_of(turn, goal)] || same_pos(goal, actor->pos))) {
            is_goal[index_of(turn, goal)] = true;
        }
    }
    if (is_goal[start]) return PathStep{actor->pos, 0, actor->pos};

    static constexpr std::array<Pos, 8> offsets = {
        Pos{-1, -1}, Pos{0, -1}, Pos{1, -1}, Pos{-1, 0},
        Pos{1, 0},   Pos{-1, 1}, Pos{0, 1},  Pos{1, 1},
    };
    std::vector<int> parent(blocked.size(), -1);
    std::vector<int> distances(blocked.size(), -1);
    std::queue<int> open;
    distances[start] = 0;
    open.push(start);

    int reached = -1;
    while (!open.empty() && reached < 0) {
        const int current = open.front();
        open.pop();
        const Pos current_pos{current % turn.map.width, current / turn.map.width};
        for (const auto& offset : offsets) {
            const Pos next{current_pos.x + offset.x, current_pos.y + offset.y};
            if (!in_bounds(turn, next)) continue;
            const int next_index = index_of(turn, next);
            if (blocked[next_index] || distances[next_index] >= 0) continue;
            if (current == start && reverses_reserved_edge(reservations, current_pos, next)) continue;
            parent[next_index] = current;
            distances[next_index] = distances[current] + 1;
            if (is_goal[next_index]) {
                reached = next_index;
                break;
            }
            open.push(next_index);
        }
    }
    if (reached < 0) return std::nullopt;

    int first = reached;
    while (parent[first] != start) first = parent[first];
    return PathStep{{first % turn.map.width, first / turn.map.width},
                    distances[reached],
                    {reached % turn.map.width, reached / turn.map.width}};
}

}  // namespace astra
