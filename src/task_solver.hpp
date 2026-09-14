#pragma once

#include "actions.hpp"

#include <vector>

namespace astra {

struct TaskCandidates {
    std::vector<CandidateAction> actions;
    std::vector<TopLevelCandidate> top_level;
    ArbitrationRules rules;
};

TaskCandidates task_candidates(const TurnObservation& turn, int priority = 4000);

}  // namespace astra
