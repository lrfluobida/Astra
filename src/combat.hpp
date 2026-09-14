#pragma once

#include "actions.hpp"

#include <optional>
#include <vector>

namespace astra {

struct AttackPlan {
    std::vector<Pos> targets;
    long long utility = 0;
};

std::optional<AttackPlan> plan_weapon_attack(const TurnObservation& turn,
                                             const UnitObservation& weapon);

std::vector<CandidateAction> combat_candidates(const TurnObservation& turn,
                                               int priority_start = 2000);

}  // namespace astra
