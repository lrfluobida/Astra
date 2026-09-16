#include "session.hpp"
#include "strategy.hpp"
#include "test_support.hpp"

#include <chrono>
#include <string>

namespace {

Json::Value session_fixture() {
    return astra::test::load_json_file("tests/fixtures/minimal_turn.json");
}

astra::Decision prompt_decision(const std::string& prompt) {
    astra::Decision decision;
    decision.prompt = prompt;
    return decision;
}

astra::Decision accept_task_decision() {
    astra::Decision decision;
    decision.role_commands[10011].action = "acceptTask";
    return decision;
}

astra::Decision summon_decision(std::initializer_list<std::pair<const int, std::string>> orders) {
    astra::Decision decision;
    for (const auto& [actor, name] : orders) {
        auto& command = decision.role_commands[actor];
        command.action = "use";
        command.name = name;
    }
    return decision;
}

bool is_valid_utf8(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        if (lead <= 0x7f) {
            length = 1;
        } else if ((lead & 0xe0) == 0xc0) {
            length = 2;
        } else if ((lead & 0xf0) == 0xe0) {
            length = 3;
        } else if ((lead & 0xf8) == 0xf0) {
            length = 4;
        } else {
            return false;
        }
        if (index + length > text.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xc0) != 0x80) {
                return false;
            }
        }
        index += length;
    }
    return true;
}

Json::Value planned_response(astra::AgentSession& session,
                             const astra::BaselineStrategy& strategy,
                             const Json::Value& input) {
    return session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        return strategy.decide(turn);
    });
}

}  // namespace

ASTRA_TEST(session_advances_once_and_returns_cached_response_for_same_round) {
    astra::AgentSession session;
    auto round_one = session_fixture();
    const auto first = session.handle(round_one, prompt_decision("first"));
    const auto duplicate = session.handle(round_one, prompt_decision("must not replace first"));

    astra::test::require(first == duplicate, "identical request must return cached response");
    astra::test::require(session.diagnostics().distinct_rounds == 1,
                         "identical request must not advance session state");
    astra::test::require(session.diagnostics().daily_llm_used == 1,
                         "identical request must not consume LLM budget twice");

    auto round_two = round_one;
    round_two["roundNo"] = 2;
    astra::Decision round_two_move;
    round_two_move.role_commands[10010].action = "move";
    round_two_move.role_commands[10010].target_positions = {{8, 25}};
    session.handle(round_two, round_two_move);
    round_two["worldNews"]["officialNews"] = "同一回合的修订消息";
    const auto conflicting = session.handle(round_two, prompt_decision("must not run"));
    astra::test::require(conflicting == astra::test::empty_response(),
                         "different payload in the same round must get a conservative response");
    astra::test::require(session.diagnostics().distinct_rounds == 2,
                         "different payload in the same round must not advance twice");
    astra::test::require(session.diagnostics().daily_llm_used == 1,
                         "same-round retry must not consume budget");
}

ASTRA_TEST(session_resets_on_round_rollback_or_team_change) {
    astra::AgentSession session;
    auto input = session_fixture();
    session.handle(input);
    const auto initial_generation = session.diagnostics().generation;

    input["roundNo"] = 3;
    session.handle(input);
    input["roundNo"] = 1;
    session.handle(input);
    astra::test::require(session.diagnostics().generation == initial_generation + 1,
                         "round rollback must reset the match generation");
    astra::test::require(session.diagnostics().distinct_rounds == 1,
                         "rollback request must become the first round of the reset state");

    input["roundNo"] = 2;
    input["teamOur"]["teamId"] = "different-team";
    session.handle(input);
    astra::test::require(session.diagnostics().generation == initial_generation + 2,
                         "team change must reset the match generation");
}

ASTRA_TEST(session_correlates_task_results_with_the_active_task_serial) {
    astra::AgentSession session;
    auto input = session_fixture();
    session.handle(input, accept_task_decision());

    input["roundNo"] = 2;
    input["phaseTask"] = "同一道题面";
    input["lastRoundRoleActionResults"] = Json::Value(Json::objectValue);
    input["lastRoundRoleActionResults"]["10011"] = true;
    session.handle(input, prompt_decision("solve task one"));
    astra::test::require(session.diagnostics().task_serial == 1,
                         "successful task acceptance must allocate a task serial");
    astra::test::require(session.diagnostics().daily_llm_used == 0,
                         "task prompt must not consume daily LLM budget");

    input["roundNo"] = 3;
    input["phaseTask"] = "";
    input["llmResp"] = "late answer for task one";
    session.handle(input, accept_task_decision());
    astra::test::require(!session.diagnostics().last_llm_result.has_value(),
                         "result arriving after task end must not attach to a stale task");

    input["roundNo"] = 4;
    input["phaseTask"] = "同一道题面";
    input["llmResp"] = "unexpected old result";
    input["lastRoundRoleActionResults"] = Json::Value(Json::objectValue);
    input["lastRoundRoleActionResults"]["10011"] = true;
    session.handle(input);
    astra::test::require(session.diagnostics().task_serial == 2,
                         "identical task text from a new acceptance must get a new serial");
    astra::test::require(!session.diagnostics().last_llm_result.has_value(),
                         "unexpected LLM response must not attach to the new task");
}

ASTRA_TEST(session_accepts_only_results_from_an_earlier_sent_request) {
    astra::AgentSession session;
    auto input = session_fixture();
    input["phaseTask"] = "sandbox task";
    input["llmResp"] = "unsolicited";
    input["lastCmdResult"] = "[exitCode:0]\nfabricated";

    astra::Decision decision;
    decision.execute_command = "printf real";
    session.handle(input, decision);
    astra::test::require(!session.diagnostics().last_llm_result.has_value(),
                         "LLM result without a pending prompt must be ignored");
    astra::test::require(!session.diagnostics().last_command_result.has_value(),
                         "same-round command result must not satisfy a new command");

    input["roundNo"] = 2;
    input["llmResp"] = "";
    input["lastCmdResult"] = "[exitCode:0]\nreal";
    session.handle(input);
    astra::test::require(session.diagnostics().last_command_result ==
                             std::optional<std::string>("[exitCode:0]\nreal"),
                         "next-round command result must satisfy the pending command");
}

ASTRA_TEST(session_passes_bounded_current_task_history_to_the_real_strategy) {
    astra::AgentSession session;
    const astra::BaselineStrategy strategy;
    auto input = session_fixture();
    input["phaseTask"] = "依次读取 alpha.txt 和 beta.txt，然后提交答案。";

    planned_response(session, strategy, input);

    input["roundNo"] = 2;
    input["llmResp"] = R"({"kind":"command","command":"cat alpha.txt"})";
    const auto alpha_command = planned_response(session, strategy, input);
    astra::test::require(alpha_command["executeCmd"] == "cat alpha.txt",
                         "the first model command must be executed");

    input["roundNo"] = 3;
    input["llmResp"] = "";
    input["lastCmdResult"] = "alpha 结果：甲";
    const auto alpha_review = planned_response(session, strategy, input);
    astra::test::require(alpha_review["prompt"].asString().find("cat alpha.txt") !=
                             std::string::npos,
                         "review prompt must associate the command with its result");

    input["roundNo"] = 4;
    input["llmResp"] = R"({"kind":"command","command":"cat beta.txt"})";
    input["lastCmdResult"] = "";
    planned_response(session, strategy, input);

    input["roundNo"] = 5;
    input["llmResp"] = "";
    input["lastCmdResult"] = "beta 结果：乙";
    const auto beta_review = planned_response(session, strategy, input);
    const std::string accumulated_prompt = beta_review["prompt"].asString();
    astra::test::require(accumulated_prompt.find("alpha 结果：甲") != std::string::npos &&
                             accumulated_prompt.find("beta 结果：乙") != std::string::npos,
                         "later review prompts must retain evidence from earlier commands");

    input["roundNo"] = 6;
    input["llmResp"] = R"({"kind":"answer","answer":"甲乙"})";
    input["lastCmdResult"] = "";
    planned_response(session, strategy, input);

    input["roundNo"] = 7;
    input["llmResp"] = "";
    input["lastRoundRoleActionResults"]["10011"] = false;
    input["errors"] = Json::Value(Json::arrayValue);
    input["errors"].append(astra::test::parse_json_text(
        R"({"errorCode":422,"description":"答案不正确，请重试"})"));
    int duplicate_planner_calls = 0;
    const auto retry = session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        ++duplicate_planner_calls;
        return strategy.decide(turn);
    });
    const std::string retry_prompt = retry["prompt"].asString();
    astra::test::require(retry_prompt.find("alpha 结果：甲") != std::string::npos &&
                             retry_prompt.find("beta 结果：乙") != std::string::npos &&
                             retry_prompt.find("甲乙") != std::string::npos &&
                             retry_prompt.find("答案不正确，请重试") != std::string::npos,
                         "wrong-answer retry must retain evidence, submitted answer, and error");

    const auto duplicate = session.handle_planned(input, [&](const astra::TurnObservation&) {
        ++duplicate_planner_calls;
        return astra::Decision{};
    });
    astra::test::require(duplicate == retry && duplicate_planner_calls == 1,
                         "duplicate input must be cached without invoking the planner");

    input["roundNo"] = 8;
    input["phaseTask"] = "全新的任务";
    input["errors"] = Json::Value(Json::arrayValue);
    const auto changed_task = planned_response(session, strategy, input);
    astra::test::require(changed_task["prompt"].asString().find("alpha 结果：甲") ==
                             std::string::npos,
                         "a changed task must not inherit evidence from the old task");

    input["roundNo"] = 1;
    const auto reset = planned_response(session, strategy, input);
    astra::test::require(reset["prompt"].asString().find("甲乙") == std::string::npos,
                         "a round rollback must clear task history");
}

ASTRA_TEST(session_drops_stale_pending_results_when_the_task_changes) {
    astra::AgentSession session;
    auto input = session_fixture();
    input["phaseTask"] = "旧任务";
    session.handle_planned(input, [](const astra::TurnObservation&) {
        astra::Decision decision;
        decision.execute_command = "printf old-evidence";
        return decision;
    });

    input["roundNo"] = 2;
    input["phaseTask"] = "新任务";
    input["lastCmdResult"] = "旧任务结果";
    astra::TurnObservation planned_turn;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        planned_turn = turn;
        return astra::Decision{};
    });

    astra::test::require(planned_turn.last_command_result.empty() &&
                             planned_turn.task_history.empty(),
                         "an old pending result must not reach the replacement task");
}

ASTRA_TEST(session_does_not_attach_a_daily_prompt_reply_to_a_new_task) {
    astra::AgentSession session;
    const astra::BaselineStrategy strategy;
    auto input = session_fixture();
    session.handle(input, prompt_decision("daily planning prompt"));

    input["roundNo"] = 2;
    input["phaseTask"] = "刚刚开始的新任务";
    input["llmResp"] = R"({"kind":"answer","answer":"旧的日常回复"})";
    const auto response = planned_response(session, strategy, input);

    astra::test::require(response.isMember("prompt") &&
                             !response["roleCommandMap"].isMember("10011"),
                         "a daily prompt reply must not become an answer for a new task");
}

ASTRA_TEST(session_bounds_task_history_without_splitting_utf8) {
    astra::AgentSession session;
    auto input = session_fixture();
    input["phaseTask"] = "持续重试任务";
    std::string payload;
    for (int index = 0; index < 3000; ++index) payload += "界";

    for (int round = 1; round <= 20; ++round) {
        input["roundNo"] = round;
        session.handle_planned(input, [&, round](const astra::TurnObservation&) {
            astra::Decision decision;
            decision.role_commands[10011].action = "submitAnswer";
            decision.role_commands[10011].task_answer =
                "answer-" + std::to_string(round) + ":" + payload;
            return decision;
        });
    }

    input["roundNo"] = 21;
    std::string history;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        history = turn.task_history;
        return astra::Decision{};
    });
    astra::test::require(history.size() <= 32768,
                         "task history must remain within the byte cap");
    astra::test::require(is_valid_utf8(history),
                         "task history truncation must preserve UTF-8 boundaries");
    astra::test::require(history.find("answer-20:") != std::string::npos &&
                             history.find("answer-1:") == std::string::npos,
                         "bounded history must retain newest entries and evict oldest entries");
}

ASTRA_TEST(session_enforces_daily_llm_budget_and_records_news_once_per_day) {
    astra::AgentSession session;
    auto input = session_fixture();
    for (int round = 1; round <= 4; ++round) {
        input["roundNo"] = round;
        input["worldNews"]["officialNews"] = "day one news " + std::to_string(round);
        const auto response = session.handle(input, prompt_decision("daily prompt"));
        if (round <= 3) {
            astra::test::require(response.isMember("prompt"),
                                 "first three daily prompts must be sent");
        } else {
            astra::test::require(!response.isMember("prompt"),
                                 "fourth daily prompt must be dropped");
        }
    }
    astra::test::require(session.diagnostics().daily_llm_used == 3,
                         "daily LLM usage must stop at three");
    astra::test::require(session.diagnostics().news_by_day.size() == 1,
                         "only the first news payload of a day must be recorded");
    astra::test::require(!session.diagnostics().pending_prompt.has_value(),
                         "a rejected prompt must not remain pending");

    input["roundNo"] = 131;
    input["worldNews"]["officialNews"] = "day two news";
    const auto next_day = session.handle(input, prompt_decision("new day prompt"));
    astra::test::require(next_day.isMember("prompt"), "new day must reset daily LLM budget");
    astra::test::require(session.diagnostics().daily_llm_used == 1,
                         "new day usage must begin at one");
    astra::test::require(session.diagnostics().news_by_day.size() == 2,
                         "new day must record its first news payload");
}

ASTRA_TEST(session_injects_and_defensively_caps_daily_summon_orders) {
    astra::AgentSession session;
    auto input = session_fixture();
    int observed_used = -1;
    for (int round = 1; round <= 4; ++round) {
        input["roundNo"] = round;
        session.handle(input, summon_decision({{10010, "SmallRobotSummonOrder"},
                                               {10012, "MiddleRobotSummonOrder"}}));
    }
    input["roundNo"] = 5;
    session.handle(input, summon_decision({{10010, "LargeRobotSummonOrder"}}));

    input["roundNo"] = 6;
    const auto capped = session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return summon_decision({{10010, "LargeRobotSummonOrder"},
                                {10012, "BossRobotSummonOrder"}});
    });
    astra::test::require(observed_used == 9,
                         "planner must observe nine conservative daily reservations");
    astra::test::require(capped["roleCommandMap"].size() == 1,
                         "session must emit only one of two summons when one slot remains");

    input["roundNo"] = 7;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return summon_decision({{10010, "SmallRobotSummonOrder"}});
    });
    astra::test::require(observed_used == 10,
                         "the final emitted summon must consume the tenth daily slot");
}

ASTRA_TEST(session_refunds_only_immediate_explicit_summon_failures) {
    astra::AgentSession session;
    auto input = session_fixture();
    session.handle(input, summon_decision({{10010, "LargeRobotSummonOrder"}}));

    input["roundNo"] = 2;
    input["lastRoundRoleActionResults"]["10010"] = false;
    int observed_used = -1;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return astra::Decision{};
    });
    astra::test::require(observed_used == 0,
                         "an explicitly failed summon must refund on the immediate next round");

    input["roundNo"] = 3;
    input["lastRoundRoleActionResults"] = Json::Value(Json::objectValue);
    session.handle(input, summon_decision({{10010, "BossRobotSummonOrder"}}));
    input["roundNo"] = 5;
    input["lastRoundRoleActionResults"]["10010"] = false;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return astra::Decision{};
    });
    astra::test::require(observed_used == 1,
                         "a result arriving after a round gap must not refund a reservation");

    input["roundNo"] = 6;
    input["lastRoundRoleActionResults"] = Json::Value(Json::objectValue);
    session.handle(input, summon_decision({{10012, "MiddleRobotSummonOrder"}}));
    input["roundNo"] = 7;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return astra::Decision{};
    });
    astra::test::require(observed_used == 2,
                         "a missing action result must conservatively keep the reservation");
}

ASTRA_TEST(session_does_not_recount_duplicate_summon_requests) {
    astra::AgentSession session;
    auto input = session_fixture();
    const auto decision = summon_decision({{10010, "SmallRobotSummonOrder"}});
    const auto first = session.handle(input, decision);
    const auto duplicate = session.handle(input, decision);
    astra::test::require(first == duplicate, "duplicate summon request must return cached response");

    input["roundNo"] = 2;
    int observed_used = -1;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return astra::Decision{};
    });
    astra::test::require(observed_used == 1,
                         "duplicate requests must not count the same emitted summon twice");
}

ASTRA_TEST(session_resets_summon_budget_on_day_match_and_identity_boundaries) {
    astra::AgentSession session;
    auto input = session_fixture();
    input["roundNo"] = 130;
    session.handle(input, summon_decision({{10010, "BossRobotSummonOrder"}}));

    int observed_used = -1;
    input["roundNo"] = 131;
    input["lastRoundRoleActionResults"]["10010"] = false;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return summon_decision({{10010, "SmallRobotSummonOrder"}});
    });
    astra::test::require(observed_used == 0,
                         "new day must start at zero without refunding the prior day");

    input["roundNo"] = 132;
    input["lastRoundRoleActionResults"] = Json::Value(Json::objectValue);
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return astra::Decision{};
    });
    astra::test::require(observed_used == 1,
                         "the first new-day summon must consume exactly one slot");

    input["roundNo"] = 1;
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return summon_decision({{10010, "LargeRobotSummonOrder"}});
    });
    astra::test::require(observed_used == 0,
                         "round rollback must reset the summon budget");

    input["roundNo"] = 2;
    input["teamOur"]["teamId"] = "replacement-team";
    session.handle_planned(input, [&](const astra::TurnObservation& turn) {
        observed_used = turn.summon_orders_used;
        return astra::Decision{};
    });
    astra::test::require(observed_used == 0,
                         "team identity change must reset the summon budget");
}

ASTRA_TEST(session_does_not_mutate_confirmed_state_for_malformed_observation) {
    astra::AgentSession session;
    auto input = session_fixture();
    session.handle(input);

    Json::Value malformed(Json::objectValue);
    malformed["roundNo"] = 999;
    malformed["teamOur"] = "broken";
    const auto response = session.handle(malformed, prompt_decision("must not be sent"));
    astra::test::require(response == astra::test::empty_response(),
                         "malformed observation must receive conservative response");
    astra::test::require(session.diagnostics().distinct_rounds == 1,
                         "malformed observation must not mutate confirmed state");

    input["roundNo"] = 2;
    session.handle(input);
    astra::test::require(session.diagnostics().distinct_rounds == 2,
                         "valid observation after malformed input must advance normally");
}

ASTRA_TEST(session_keeps_baseline_when_search_exceeds_budget) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::time_point{};
    int clock_calls = 0;
    astra::AgentSession session([&] {
        ++clock_calls;
        return clock_calls < 3 ? start : start + std::chrono::milliseconds(8);
    });

    astra::Decision baseline;
    baseline.role_commands[10010].action = "move";
    baseline.role_commands[10010].target_positions = {{8, 25}};
    bool search_called = false;
    const auto response = session.handle_with_budget(
        session_fixture(),
        baseline,
        std::chrono::milliseconds(5),
        [&] {
            search_called = true;
            return prompt_decision("late refinement");
        });

    astra::test::require(search_called, "search must run while initial budget remains");
    astra::test::require(response["roleCommandMap"].isMember("10010"),
                         "late search result must not replace baseline response");
    astra::test::require(!response.isMember("prompt"),
                         "late search prompt must not be sent");
    astra::test::require(session.diagnostics().degradation_reason.find("budget") !=
                             std::string::npos,
                         "budget degradation must be observable");
}
