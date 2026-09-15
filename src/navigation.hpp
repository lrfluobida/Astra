#pragma once

#include "protocol.hpp"

#include <optional>
#include <vector>

namespace astra {

struct MovementEdge {
    Pos from;
    Pos to;
};

struct NavigationReservations {
    std::vector<Pos> destinations;
    std::vector<MovementEdge> edges;
};

struct PathStep {
    Pos next;
    int distance = 0;
    Pos goal;
};

std::vector<Pos> occupied_cells(const UnitObservation& unit);
std::vector<Pos> interaction_cells(const TurnObservation& turn, Pos target);
std::vector<Pos> station_interaction_cells(const TurnObservation& turn);
std::vector<Pos> task_interaction_cells(const TurnObservation& turn,
                                       const TaskPointObservation& task);

std::optional<PathStep> next_step_toward_any(const TurnObservation& turn,
                                             int actor_id,
                                             const std::vector<Pos>& goals,
                                             const NavigationReservations& reservations);

}  // namespace astra
