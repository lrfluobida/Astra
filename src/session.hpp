#pragma once

#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
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
    using Clock = std::chrono::steady_clock;
    using ClockFunction = std::function<Clock::time_point()>;
    using SearchFunction = std::function<Decision()>;
    using PlannerFunction = std::function<Decision(const TurnObservation&)>;

    AgentSession();
    explicit AgentSession(ClockFunction clock);

    Json::Value handle(const Json::Value& input, Decision proposed = {});
    Json::Value handle_planned(const Json::Value& input, const PlannerFunction& planner);
    Json::Value handle_with_budget(const Json::Value& input,
                                      Decision baseline,
                                      std::chrono::milliseconds budget,
                                      const SearchFunction& search);
    const SessionDiagnostics& diagnostics() const;

private:
    void append_task_history(const std::string& kind, const std::string& content);
    void clear_task_history();
    std::string task_history() const;
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
    std::deque<std::string> task_history_entries_;
    std::string pending_command_text_;
    Json::Value last_request_;
    Json::Value last_response_;
    ClockFunction clock_;
};

}  // namespace astra
