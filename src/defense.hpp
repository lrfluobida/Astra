#pragma once

#include "protocol.hpp"

#include <array>
#include <optional>
#include <vector>

namespace astra {

struct DefenseLayout {
    std::vector<Pos> weapon_build_tiles;
    std::vector<Pos> wall_build_tiles;
    std::vector<Pos> front_wall_tiles;
    Pos near_rocket;
    std::array<Pos, 2> far_rockets;
};

std::optional<DefenseLayout> derive_defense_layout(const TurnObservation& turn);

}  // namespace astra
