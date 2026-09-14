#include "session.hpp"
#include "test_support.hpp"

#include <chrono>
#include <fstream>
#include <string>

namespace {

nlohmann::json session_fixture() {
    std::ifstream input("tests/fixtures/minimal_turn.json");
    astra::test::require(input.good(), "minimal_turn.json must be readable");
    return nlohmann::json::parse(input);
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
    session.handle(round_two);
    round_two["worldNews"]["officialNews"] = "同一回合的修订消息";
    session.handle(round_two, prompt_decision("must not run"));
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
    input["lastRoundRoleActionResults"] = {{"10011", true}};
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
    input["lastRoundRoleActionResults"] = {{"10011", true}};
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

ASTRA_TEST(session_enforces_daily_llm_budget_and_records_news_once_per_day) {
    astra::AgentSession session;
    auto input = session_fixture();
    for (int round = 1; round <= 4; ++round) {
        input["roundNo"] = round;
        input["worldNews"]["officialNews"] = "day one news " + std::to_string(round);
        const auto response = session.handle(input, prompt_decision("daily prompt"));
        if (round <= 3) {
            astra::test::require(response.contains("prompt"),
                                 "first three daily prompts must be sent");
        } else {
            astra::test::require(!response.contains("prompt"),
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
    astra::test::require(next_day.contains("prompt"), "new day must reset daily LLM budget");
    astra::test::require(session.diagnostics().daily_llm_used == 1,
                         "new day usage must begin at one");
    astra::test::require(session.diagnostics().news_by_day.size() == 2,
                         "new day must record its first news payload");
}

ASTRA_TEST(session_does_not_mutate_confirmed_state_for_malformed_observation) {
    astra::AgentSession session;
    auto input = session_fixture();
    session.handle(input);

    const nlohmann::json malformed = {{"roundNo", 999}, {"teamOur", "broken"}};
    const auto response = session.handle(malformed, prompt_decision("must not be sent"));
    astra::test::require(response ==
                             nlohmann::json{{"roleCommandMap", nlohmann::json::object()}},
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
    astra::test::require(response["roleCommandMap"].contains("10010"),
                         "late search result must not replace baseline response");
    astra::test::require(!response.contains("prompt"),
                         "late search prompt must not be sent");
    astra::test::require(session.diagnostics().degradation_reason.find("budget") !=
                             std::string::npos,
                         "budget degradation must be observable");
}
