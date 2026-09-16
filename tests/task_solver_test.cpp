#include "task_solver.hpp"
#include "test_support.hpp"

#include <string>

namespace {

astra::TurnObservation task_turn() {
    astra::TurnObservation turn;
    turn.round_no = 20;
    turn.map.width = 41;
    turn.map.height = 32;
    turn.phase_task = "读取本地 weather.json，返回北京温度，答案必须是 JSON。";
    turn.team_our.type = "challenger";
    turn.team_our.team_id = "astra-test";
    turn.team_our.team_name = "Astra";
    astra::UnitObservation pioneer;
    pioneer.id = 10011;
    pioneer.pos = {14, 14};
    pioneer.role_type = astra::RoleType::pioneer;
    pioneer.role_type_raw = "pioneer";
    pioneer.health = 200;
    pioneer.backpack_capacity = 40;
    pioneer.owned = true;
    turn.team_our.roles.push_back(pioneer);
    return turn;
}

}  // namespace

ASTRA_TEST(task_solver_builds_strict_initial_prompt_only_for_live_task) {
    auto turn = task_turn();
    const auto planned = astra::task_candidates(turn);
    astra::test::require(planned.actions.empty() && planned.top_level.size() == 1,
                         "new active task must request one model response");
    const auto& prompt = planned.top_level.front().value;
    astra::test::require(prompt.find(turn.phase_task) != std::string::npos,
                         "initial prompt must preserve the complete task text");
    astra::test::require(prompt.find("\"kind\":\"answer\"") != std::string::npos &&
                             prompt.find("\"kind\":\"command\"") != std::string::npos &&
                             prompt.find("无法访问外部网络") != std::string::npos,
                         "initial prompt must define answer, command, and offline constraints");

    turn.phase_task.clear();
    astra::test::require(astra::task_candidates(turn).top_level.empty(),
                         "idle turns must not spend model calls");
    turn = task_turn();
    turn.team_our.roles.front().health = 0;
    astra::test::require(astra::task_candidates(turn).top_level.empty(),
                         "task without a living pioneer must not call the model");
}

ASTRA_TEST(task_solver_parses_structured_answers_and_repairs_plain_output) {
    auto turn = task_turn();
    turn.llm_response =
        "```json\n{\"kind\":\"answer\",\"answer\":{\"city\":\"北京\",\"temperature\":18}}\n```";
    const auto structured = astra::task_candidates(turn);
    astra::test::require(structured.actions.size() == 1 &&
                             structured.actions.front().command.action == "submitAnswer" &&
                             structured.actions.front().command.task_answer ==
                                 "{\"city\":\"北京\",\"temperature\":18}",
                         "structured answer must preserve its JSON object as taskAnswer");

    turn.llm_response = "北京当前温度为18摄氏度";
    const auto plain = astra::task_candidates(turn);
    astra::test::require(plain.actions.empty() && plain.top_level.size() == 1 &&
                             plain.top_level.front().kind == astra::TopLevelKind::prompt,
                         "unstructured output must be confirmed in the answer protocol before submission");
}

ASTRA_TEST(task_solver_does_not_submit_model_analysis_or_shell_as_an_answer) {
    auto turn = task_turn();
    for (const std::string response : {"I need to inspect the files first.",
                                       "```bash\nls -la\n```", "   ",
                                       "{\"answer\":\"not final\",\"command\":\"ls\"}",
                                       "echo '{\"answer\":\"not final\"}'",
                                       "I might return {\"answer\":\"18\"}, but must verify first."}) {
        turn.llm_response = response;
        const auto result = astra::task_candidates(turn);
        astra::test::require(result.actions.empty() && result.top_level.size() == 1 &&
                                 result.top_level.front().kind == astra::TopLevelKind::prompt,
                             "analysis, shell text, and blank model output must never be submitted");
    }
}

ASTRA_TEST(task_solver_executes_model_command_then_requests_evidence_review) {
    auto turn = task_turn();
    turn.llm_response =
        R"({"kind":"command","command":"python3 -c 'import json; print(json.load(open(\"weather.json\")))'"})";
    const auto command = astra::task_candidates(turn);
    astra::test::require(command.actions.empty() && command.top_level.size() == 1 &&
                             command.top_level.front().kind ==
                                 astra::TopLevelKind::execute_command,
                         "command response must become one executeCmd candidate");
    astra::test::require(command.top_level.front().value.find("python3") == 0,
                         "executeCmd must preserve the model command");

    turn.llm_response.clear();
    turn.last_command_result = "[exitCode:0]\n{\"北京\": 18}";
    const auto review = astra::task_candidates(turn);
    astra::test::require(review.top_level.size() == 1 &&
                             review.top_level.front().kind == astra::TopLevelKind::prompt &&
                             review.top_level.front().value.find(turn.last_command_result) !=
                                 std::string::npos &&
                             review.top_level.front().value.find(turn.phase_task) !=
                                 std::string::npos,
                         "command result must be reviewed together with the original task");
}

ASTRA_TEST(task_solver_repairs_malformed_structured_output) {
    auto turn = task_turn();
    turn.task_history = "[command/result]\ncommand: cat alpha\nresult: 阿尔法";
    turn.llm_response = "{\"kind\":\"answer\",\"answer\":18";
    const auto planned = astra::task_candidates(turn);
    astra::test::require(planned.actions.empty() && planned.top_level.size() == 1 &&
                             planned.top_level.front().kind == astra::TopLevelKind::prompt &&
                             planned.top_level.front().value.find("格式修复") !=
                                 std::string::npos &&
                             planned.top_level.front().value.find(turn.task_history) !=
                                 std::string::npos &&
                             planned.top_level.front().value.find(turn.llm_response) !=
                                 std::string::npos,
                         "malformed JSON-like output must trigger a targeted repair prompt");
}

ASTRA_TEST(task_solver_provides_active_task_arbitration_rules) {
    const auto planned = astra::task_candidates(task_turn());
    astra::test::require(planned.rules.task_active && planned.rules.pioneer_id == 10011,
                         "task candidates must identify the active pioneer for arbitration");
    astra::test::require(planned.top_level.front().requires_active_task,
                         "task prompt must be guarded by active task state");
}
