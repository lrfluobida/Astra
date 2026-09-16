#include "session.hpp"

#include <sstream>
#include <utility>

namespace astra {
namespace {

constexpr int kRoundsPerDay = 130;
constexpr int kDailyLlmLimit = 3;
constexpr int kDailySummonOrderLimit = 10;
constexpr std::size_t kMaximumTaskHistoryEntries = 16;
constexpr std::size_t kMaximumTaskHistoryBytes = 32768;
constexpr std::size_t kMaximumTaskHistoryEventBytes = 8192;

int game_day(int round_no) {
    return (round_no - 1) / kRoundsPerDay;
}

bool action_succeeded(const TurnObservation& turn, int actor_id) {
    const auto found = turn.last_round_role_action_results.find(actor_id);
    return found != turn.last_round_role_action_results.end() && found->second;
}

astra::Optional<int> accepted_task_actor(const Decision& decision) {
    for (const auto& actor_id_entry : decision.role_commands) {
        const auto& actor_id = actor_id_entry.first;
        const auto& command = actor_id_entry.second;
        if (command.action == "acceptTask") return actor_id;
    }
    return astra::nullopt;
}

std::string utf8_prefix(const std::string& value, std::size_t maximum_bytes) {
    if (value.size() <= maximum_bytes) return value;
    std::size_t end = maximum_bytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) {
        --end;
    }
    return value.substr(0, end);
}

}  // namespace

AgentSession::AgentSession() : AgentSession([] { return Clock::now(); }) {}

AgentSession::AgentSession(ClockFunction clock) : clock_(std::move(clock)) {}

Json::Value AgentSession::handle_with_budget(const Json::Value& input,
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

Json::Value AgentSession::handle(const Json::Value& input, Decision proposed) {
    return handle_planned(input, [&proposed](const TurnObservation&) mutable {
        return std::move(proposed);
    });
}

Json::Value AgentSession::handle_planned(const Json::Value& input,
                                         const PlannerFunction& planner) {
    const ParseResult parsed = parse_turn(input);
    if (!parsed.turn || !parsed.errors.empty()) {
        return encode_response(Decision{});
    }
    TurnObservation turn = *parsed.turn;

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
        daily_summon_orders_used_ = 0;
        pending_summon_orders_.reset();
    } else if (pending_summon_orders_) {
        if (pending_summon_orders_->day == day &&
            pending_summon_orders_->sent_round + 1 == turn.round_no) {
            for (const int actor : pending_summon_orders_->actors) {
                const auto result = turn.last_round_role_action_results.find(actor);
                if (result != turn.last_round_role_action_results.end() && !result->second &&
                    daily_summon_orders_used_ > 0) {
                    --daily_summon_orders_used_;
                }
            }
        }
        pending_summon_orders_.reset();
    }
    turn.summon_orders_used = daily_summon_orders_used_;
    diagnostics_.news_by_day.emplace(day, turn.world_news);

    bool task_changed = false;
    if (turn.phase_task.empty()) {
        task_changed = active_task_serial_.has_value() || !task_history_entries_.empty();
        active_task_serial_.reset();
    } else if (!active_task_serial_) {
        const bool accepted = pending_accept_actor_ && pending_accept_round_ &&
                              *pending_accept_round_ < turn.round_no &&
                              action_succeeded(turn, *pending_accept_actor_);
        if (accepted || previous_phase_task_.empty()) {
            ++diagnostics_.task_serial;
            active_task_serial_ = diagnostics_.task_serial;
            task_changed = true;
        }
    } else if (!previous_phase_task_.empty() && previous_phase_task_ != turn.phase_task) {
        ++diagnostics_.task_serial;
        active_task_serial_ = diagnostics_.task_serial;
        task_changed = true;
    }
    if (task_changed) clear_task_history();

    bool accepted_llm_result = false;
    if (diagnostics_.pending_prompt &&
        diagnostics_.pending_prompt->sent_round < turn.round_no) {
        const bool task_matches =
            diagnostics_.pending_prompt->task_serial == active_task_serial_;
        if (task_matches && !turn.llm_response.empty()) {
            diagnostics_.last_llm_result = turn.llm_response;
            accepted_llm_result = true;
        }
        diagnostics_.pending_prompt.reset();
    }
    if (!accepted_llm_result) turn.llm_response.clear();

    bool accepted_command_result = false;
    if (diagnostics_.pending_command &&
        diagnostics_.pending_command->sent_round < turn.round_no) {
        const bool task_matches = diagnostics_.pending_command->task_serial == active_task_serial_;
        if (task_matches && !turn.last_command_result.empty()) {
            diagnostics_.last_command_result = turn.last_command_result;
            append_task_history("command/result",
                                "command: " + pending_command_text_ + "\nresult: " +
                                    turn.last_command_result);
            accepted_command_result = true;
        }
        diagnostics_.pending_command.reset();
        pending_command_text_.clear();
    }
    if (!accepted_command_result) turn.last_command_result.clear();

    if (active_task_serial_ && !task_changed) {
        for (const auto& error : turn.errors) {
            append_task_history("error",
                                "code=" + std::to_string(error.code) + ": " +
                                    error.description);
        }
    }
    turn.task_history = task_history();

    Decision proposed = planner(turn);

    std::vector<int> emitted_summon_actors;
    for (auto command = proposed.role_commands.begin(); command != proposed.role_commands.end();) {
        const bool robot_summon = command->second.action == "use" && command->second.name &&
                                  is_robot_summon_order(*command->second.name);
        if (robot_summon && daily_summon_orders_used_ >= kDailySummonOrderLimit) {
            command = proposed.role_commands.erase(command);
            continue;
        }
        if (robot_summon) {
            ++daily_summon_orders_used_;
            emitted_summon_actors.push_back(command->first);
        }
        ++command;
    }
    if (emitted_summon_actors.empty()) {
        pending_summon_orders_.reset();
    } else {
        pending_summon_orders_ = PendingSummonOrders{day, turn.round_no,
                                                      std::move(emitted_summon_actors)};
    }

    diagnostics_.degradation_reason.clear();
    if (proposed.prompt) {
        if (active_task_serial_) {
            diagnostics_.pending_prompt = PendingRequest{turn.round_no, active_task_serial_};
        } else if (diagnostics_.daily_llm_used < kDailyLlmLimit) {
            ++diagnostics_.daily_llm_used;
            diagnostics_.pending_prompt = PendingRequest{turn.round_no, astra::nullopt};
        } else {
            proposed.prompt.reset();
            diagnostics_.degradation_reason = "daily LLM limit reached";
        }
    }

    if (proposed.execute_command) {
        if (active_task_serial_) {
            diagnostics_.pending_command = PendingRequest{turn.round_no, active_task_serial_};
            pending_command_text_ = *proposed.execute_command;
        } else {
            proposed.execute_command.reset();
            diagnostics_.degradation_reason = "executeCmd requires an active task";
        }
    }

    if (active_task_serial_) {
        for (const auto& actor_id_entry : proposed.role_commands) {
            const auto& actor_id = actor_id_entry.first;
            const auto& command = actor_id_entry.second;
            (void)actor_id;
            if (command.action == "submitAnswer" && command.task_answer) {
                append_task_history("submitted answer", *command.task_answer);
            }
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

void AgentSession::append_task_history(const std::string& kind, const std::string& content) {
    if (content.empty()) return;
    const std::string prefix = "[" + kind + "]\n";
    task_history_entries_.push_back(
        prefix + utf8_prefix(content, kMaximumTaskHistoryEventBytes - prefix.size()));
    while (task_history_entries_.size() > kMaximumTaskHistoryEntries ||
           task_history().size() > kMaximumTaskHistoryBytes) {
        task_history_entries_.pop_front();
    }
}

void AgentSession::clear_task_history() {
    task_history_entries_.clear();
    pending_command_text_.clear();
}

std::string AgentSession::task_history() const {
    std::ostringstream result;
    for (auto entry = task_history_entries_.begin(); entry != task_history_entries_.end(); ++entry) {
        if (entry != task_history_entries_.begin()) result << "\n\n";
        result << *entry;
    }
    return result.str();
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
    daily_summon_orders_used_ = 0;
    pending_summon_orders_.reset();
    team_id_.clear();
    team_type_.clear();
    previous_phase_task_.clear();
    active_task_serial_.reset();
    pending_accept_actor_.reset();
    pending_accept_round_.reset();
    clear_task_history();
    last_request_ = Json::Value();
    last_response_ = Json::Value();
}

}  // namespace astra
