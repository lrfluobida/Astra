#pragma once

#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include "optional.hpp"
#include <string>
#include <vector>

namespace astra {

struct PendingRequest {
    PendingRequest() = default;
    PendingRequest(int round, astra::Optional<int> serial) : sent_round(round), task_serial(serial) {}
    int sent_round = 0;
    astra::Optional<int> task_serial;
};

struct SessionDiagnostics {
    std::uint64_t generation = 0;
    int distinct_rounds = 0;
    int task_serial = 0;
    int daily_llm_used = 0;
    std::map<int, WorldNewsObservation> news_by_day;
    astra::Optional<PendingRequest> pending_prompt;
    astra::Optional<PendingRequest> pending_command;
    astra::Optional<std::string> last_llm_result;
    astra::Optional<std::string> last_command_result;
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
    struct PendingSummonOrders {
        PendingSummonOrders() = default;
        PendingSummonOrders(int day_value, int round, std::vector<int> actor_ids)
            : day(day_value), sent_round(round), actors(std::move(actor_ids)) {}
        int day = 0;
        int sent_round = 0;
        std::vector<int> actors;
    };

    void append_task_history(const std::string& kind, const std::string& content);
    void clear_task_history();
    std::string task_history() const;
    void reset_match();

    SessionDiagnostics diagnostics_;
    astra::Optional<int> last_round_;
    astra::Optional<int> current_day_;
    int daily_summon_orders_used_ = 0;
    astra::Optional<PendingSummonOrders> pending_summon_orders_;
    std::string team_id_;
    std::string team_type_;
    std::string previous_phase_task_;
    astra::Optional<int> active_task_serial_;
    astra::Optional<int> pending_accept_actor_;
    astra::Optional<int> pending_accept_round_;
    std::deque<std::string> task_history_entries_;
    std::string pending_command_text_;
    Json::Value last_request_;
    Json::Value last_response_;
    ClockFunction clock_;
};

}  // namespace astra
