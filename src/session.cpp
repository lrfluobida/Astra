#include "session.hpp"

#include <utility>

namespace astra {
namespace {

constexpr int kRoundsPerDay = 130;
constexpr int kDailyLlmLimit = 3;

int game_day(int round_no) {
    return (round_no - 1) / kRoundsPerDay;
}

bool action_succeeded(const TurnObservation& turn, int actor_id) {
    const auto found = turn.last_round_role_action_results.find(actor_id);
    return found != turn.last_round_role_action_results.end() && found->second;
}

std::optional<int> accepted_task_actor(const Decision& decision) {
    for (const auto& [actor_id, command] : decision.role_commands) {
        if (command.action == "acceptTask") return actor_id;
    }
    return std::nullopt;
}

}  // namespace

AgentSession::AgentSession() : AgentSession([] { return Clock::now(); }) {}

AgentSession::AgentSession(ClockFunction clock) : clock_(std::move(clock)) {}

nlohmann::json AgentSession::handle_with_budget(const nlohmann::json& input,
                                                Decision baseline,
                                                std::chrono::milliseconds budget,
                                                const SearchFunction& search) {
    const auto deadline = clock_() + budget;
    if (clock_() >= deadline) {
        auto response = handle(input, std::move(baseline));
        diagnostics_.degradation_reason = "decision search budget expired before search";
        return response;
    }

    try {
        Decision refined = search();
        if (clock_() < deadline) return handle(input, std::move(refined));
        auto response = handle(input, std::move(baseline));
        diagnostics_.degradation_reason = "decision search budget expired; baseline retained";
        return response;
    } catch (const std::exception&) {
        auto response = handle(input, std::move(baseline));
        diagnostics_.degradation_reason = "decision search failed; baseline retained";
        return response;
    }
}

nlohmann::json AgentSession::handle(const nlohmann::json& input, Decision proposed) {
    const ParseResult parsed = parse_turn(input);
    if (!parsed.turn || !parsed.errors.empty()) {
        return encode_response(Decision{});
    }
    const TurnObservation& turn = *parsed.turn;

    const bool identity_changed =
        last_round_ && (team_id_ != turn.team_our.team_id || team_type_ != turn.team_our.type);
    const bool round_rolled_back = last_round_ && turn.round_no < *last_round_;
    if (identity_changed || round_rolled_back) {
        reset_match();
    }

    if (last_round_ && turn.round_no == *last_round_) {
        return last_request_ == input ? last_response_ : encode_response(Decision{});
    }

    if (!last_round_) {
        team_id_ = turn.team_our.team_id;
        team_type_ = turn.team_our.type;
    }

    const int day = game_day(turn.round_no);
    if (!current_day_ || *current_day_ != day) {
        current_day_ = day;
        diagnostics_.daily_llm_used = 0;
    }
    diagnostics_.news_by_day.try_emplace(day, turn.world_news);

    if (turn.phase_task.empty()) {
        active_task_serial_.reset();
    } else if (!active_task_serial_) {
        const bool accepted = pending_accept_actor_ && pending_accept_round_ &&
                              *pending_accept_round_ < turn.round_no &&
                              action_succeeded(turn, *pending_accept_actor_);
        if (accepted || previous_phase_task_.empty()) {
            ++diagnostics_.task_serial;
            active_task_serial_ = diagnostics_.task_serial;
        }
    } else if (!previous_phase_task_.empty() && previous_phase_task_ != turn.phase_task) {
        ++diagnostics_.task_serial;
        active_task_serial_ = diagnostics_.task_serial;
    }

    if (diagnostics_.pending_prompt &&
        diagnostics_.pending_prompt->sent_round < turn.round_no) {
        const bool task_matches = !diagnostics_.pending_prompt->task_serial ||
                                  diagnostics_.pending_prompt->task_serial == active_task_serial_;
        if (task_matches && !turn.llm_response.empty()) {
            diagnostics_.last_llm_result = turn.llm_response;
        }
        diagnostics_.pending_prompt.reset();
    }
    if (diagnostics_.pending_command &&
        diagnostics_.pending_command->sent_round < turn.round_no) {
        const bool task_matches = diagnostics_.pending_command->task_serial == active_task_serial_;
        if (task_matches && !turn.last_command_result.empty()) {
            diagnostics_.last_command_result = turn.last_command_result;
        }
        diagnostics_.pending_command.reset();
    }

    diagnostics_.degradation_reason.clear();
    if (proposed.prompt) {
        if (active_task_serial_) {
            diagnostics_.pending_prompt = PendingRequest{turn.round_no, active_task_serial_};
        } else if (diagnostics_.daily_llm_used < kDailyLlmLimit) {
            ++diagnostics_.daily_llm_used;
            diagnostics_.pending_prompt = PendingRequest{turn.round_no, std::nullopt};
        } else {
            proposed.prompt.reset();
            diagnostics_.degradation_reason = "daily LLM limit reached";
        }
    }

    if (proposed.execute_command) {
        if (active_task_serial_) {
            diagnostics_.pending_command = PendingRequest{turn.round_no, active_task_serial_};
        } else {
            proposed.execute_command.reset();
            diagnostics_.degradation_reason = "executeCmd requires an active task";
        }
    }

    pending_accept_actor_ = accepted_task_actor(proposed);
    if (pending_accept_actor_) {
        pending_accept_round_ = turn.round_no;
    } else {
        pending_accept_round_.reset();
    }

    last_request_ = input;
    last_response_ = encode_response(proposed);
    last_round_ = turn.round_no;
    previous_phase_task_ = turn.phase_task;
    ++diagnostics_.distinct_rounds;
    return last_response_;
}

const SessionDiagnostics& AgentSession::diagnostics() const {
    return diagnostics_;
}

void AgentSession::reset_match() {
    const std::uint64_t next_generation = diagnostics_.generation + 1;
    diagnostics_ = SessionDiagnostics{};
    diagnostics_.generation = next_generation;
    last_round_.reset();
    current_day_.reset();
    team_id_.clear();
    team_type_.clear();
    previous_phase_task_.clear();
    active_task_serial_.reset();
    pending_accept_actor_.reset();
    pending_accept_round_.reset();
    last_request_ = nlohmann::json();
    last_response_ = nlohmann::json();
}

}  // namespace astra
