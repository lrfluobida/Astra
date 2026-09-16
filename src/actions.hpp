#pragma once

#include "protocol.hpp"

#include <map>
#include "optional.hpp"
#include <string>
#include <vector>

namespace astra {

struct Reservation {
    Reservation() = default;
    Reservation(int money, std::map<int, std::map<std::string, int>> inventory)
        : gold(money), items(std::move(inventory)) {}
    int gold = 0;
    std::map<int, std::map<std::string, int>> items;
};

struct CandidateAction {
    CandidateAction() = default;
    CandidateAction(int key, RoleCommand action, Reservation resources, int rank, std::string origin)
        : action_key(key), command(std::move(action)), reservation(std::move(resources)),
          priority(rank), source(std::move(origin)) {}
    int action_key = 0;
    RoleCommand command;
    Reservation reservation;
    int priority = 0;
    std::string source;
};

enum class TopLevelKind {
    prompt,
    execute_command,
};

struct TopLevelCandidate {
    TopLevelCandidate() = default;
    TopLevelCandidate(TopLevelKind action_kind, std::string text, int rank, bool active,
                      astra::Optional<int> pioneer = astra::nullopt)
        : kind(action_kind), value(std::move(text)), priority(rank), requires_active_task(active),
          pioneer_id(pioneer) {}
    TopLevelKind kind = TopLevelKind::prompt;
    std::string value;
    int priority = 0;
    bool requires_active_task = false;
    astra::Optional<int> pioneer_id;
};

struct ArbitrationRules {
    std::vector<Pos> weapon_build_tiles;
    std::vector<Pos> wall_build_tiles;
    int daily_llm_remaining = 3;
    bool task_active = false;
    astra::Optional<int> pioneer_id;
};

struct RejectedCandidate {
    std::string source;
    std::string reason;
};

struct ArbitrationResult {
    Decision decision;
    std::vector<RejectedCandidate> rejected;
    std::map<int, int> actor_uses;
    int reserved_gold = 0;

    int actor_use_count(int actor_id) const;
    int gold_reserved() const;
};

ArbitrationResult arbitrate(const TurnObservation& turn,
                            const std::vector<CandidateAction>& actions,
                            const std::vector<TopLevelCandidate>& top_level,
                            const ArbitrationRules& rules);

}  // namespace astra
