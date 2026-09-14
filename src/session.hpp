#pragma once

#include "protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace astra {

struct PendingRequest {
    int sent_round = 0;
    std::optional<int> task_serial;
};

struct SessionDiagnostics {
    std::uint64_t generation = 0;
    int distinct_rounds = 0;
    int task_serial = 0;
    int daily_llm_used = 0;
    std::map<int, WorldNewsObservation> news_by_day;
    std::optional<PendingRequest> pending_prompt;
    std::optional<PendingRequest> pending_command;
    std::optional<std::string> last_llm_result;
    std::optional<std::string> last_command_result;
    std::string degradation_reason;
};

class AgentSession {
public:
    nlohmann::json handle(const nlohmann::json& input, Decision proposed = {});
    const SessionDiagnostics& diagnostics() const;

private:
    void reset_match();

    SessionDiagnostics diagnostics_;
    std::optional<int> last_round_;
    std::optional<int> current_day_;
    std::string team_id_;
    std::string team_type_;
    std::string previous_phase_task_;
    std::optional<int> active_task_serial_;
    std::optional<int> pending_accept_actor_;
    std::optional<int> pending_accept_round_;
    nlohmann::json last_request_;
    nlohmann::json last_response_;
};

}  // namespace astra
