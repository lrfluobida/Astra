#include "match_log.hpp"
#include "session.hpp"
#include "test_support.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<Json::Value> parse_log_lines(const std::string& text) {
    const std::string prefix = "ASTRA_LOG ";
    std::vector<Json::Value> events;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        astra::test::require(line.compare(0, prefix.size(), prefix) == 0,
                             "runtime log line is missing ASTRA_LOG prefix");
        events.push_back(astra::test::parse_json_text(line.substr(prefix.size())));
    }
    return events;
}

Json::Value turn_fixture() {
    return astra::test::load_json_file("tests/fixtures/minimal_turn.json");
}

Json::Value record_round(astra::AgentSession& session,
                         astra::MatchLogger& logger,
                         const Json::Value& request,
                         astra::Decision decision) {
    const astra::SessionDiagnostics before = session.diagnostics();
    const Json::Value response = session.handle(request, std::move(decision));
    logger.log_round(request,
                     response,
                     before,
                     session.diagnostics(),
                     std::chrono::microseconds(125));
    return response;
}

}  // namespace

ASTRA_TEST(match_log_emits_compact_round_summary_and_utf8_safe_chunks) {
    Json::Value request = turn_fixture();
    request["roundNo"] = 131;
    request["phaseTask"] = "第一行\n第二行：北京温度是多少？继续读取实际文件。";
    request["teamOur"]["roles"][1]["backpack"].append("铜矿");
    request["teamOur"]["roles"][1]["backpack"].append("铜矿");
    request["robot"]["roles"].append(astra::test::parse_json_text(
        R"({"id":30001,"pos":{"x":8,"y":8},"roleType":"smallRobot","health":40,"abnormalState":"","targetTeam":"challenger"})"));
    request["robot"]["roles"].append(astra::test::parse_json_text(
        R"({"id":30002,"pos":{"x":9,"y":8},"roleType":"smallRobot","health":40,"abnormalState":"","targetTeam":"defender"})"));
    request["robot"]["roles"].append(astra::test::parse_json_text(
        R"({"id":30003,"pos":{"x":10,"y":8},"roleType":"smallRobot","health":40,"abnormalState":""})"));
    request["robot"]["roles"].append(astra::test::parse_json_text(
        R"({"id":30004,"pos":{"x":11,"y":8},"roleType":"smallRobot","health":40,"abnormalState":"","targetTeam":"criminal"})"));
    request["robot"]["roles"].append(astra::test::parse_json_text(
        R"({"id":30005,"pos":{"x":12,"y":8},"roleType":"smallRobot","health":40,"abnormalState":"","targetTeam":"defender"})"));
    request["lastRoundRoleActionResults"]["10011"] = false;

    astra::Decision decision;
    decision.prompt = "请读取文件。\n保持中文原样。";
    decision.role_commands[10010].action = "move";
    decision.role_commands[10010].target_positions.push_back({8, 25});
    decision.role_commands[10010].target_positions.push_back({9, 25});
    decision.role_commands[10010].target_positions.push_back({10, 25});

    std::ostringstream output;
    astra::MatchLogger logger(output, 17, 256 * 1024);
    astra::AgentSession session;
    record_round(session, logger, request, decision);

    const std::vector<Json::Value> events = parse_log_lines(output.str());
    astra::test::require(!events.empty(), "logger emitted no events");
    const Json::Value& summary = events.front();
    astra::test::require(summary["event"].asString() == "round" &&
                             summary["schema"].asString() == "astra.match_log" &&
                             summary["version"].asInt() == 1,
                         "round summary has no stable schema identity");
    astra::test::require(summary["request"]["parse_status"].asString() == "ok" &&
                             !summary["request"]["duplicate"].asBool(),
                         "valid unique request was classified incorrectly");
    astra::test::require(summary["match"]["round"].asInt() == 131 &&
                             summary["match"]["day"].asInt() == 2 &&
                             summary["economy"]["gold"].asInt() == 75,
                         "round identity or economy is missing");
    astra::test::require(summary["robots"]["targeting_our_team"].asInt() == 1 &&
                             summary["robots"]["targeting_opponent"].asInt() == 2 &&
                             summary["robots"]["unknown"].asInt() == 2,
                         "live robots were not classified by observed target team");
    astra::test::require(summary["previous_actions"]["illegal"].asInt() == 1 &&
                             summary["task"]["answer_correct"].isNull(),
                         "action legality was conflated with answer correctness");
    astra::test::require(summary["outgoing_actions"].size() == 1 &&
                             summary["outgoing_actions"][0u]["targets"].size() == 3 &&
                             !summary["outgoing_actions"][0u]["targets_truncated"].asBool() &&
                             summary["task"]["prompt_emitted"].asBool(),
                         "outgoing action targets or prompt state is missing");

    std::string rebuilt_task;
    int task_chunks = 0;
    for (std::size_t index = 1; index < events.size(); ++index) {
        const Json::Value& event = events[index];
        if (event["event"].asString() == "text" && event["kind"].asString() == "task") {
            rebuilt_task += event["content"].asString();
            ++task_chunks;
            astra::test::require(event["original_bytes"].asUInt64() ==
                                     request["phaseTask"].asString().size(),
                                 "chunk did not retain the original UTF-8 byte count");
        }
    }
    astra::test::require(task_chunks > 1 && rebuilt_task == request["phaseTask"].asString(),
                         "escaped newline/Chinese task chunks did not reassemble exactly");
}

ASTRA_TEST(match_log_records_task_pipeline_platform_error_and_duplicate_without_repeating_text) {
    std::ostringstream output;
    astra::MatchLogger logger(output, 2048, 256 * 1024);
    astra::AgentSession session;
    Json::Value request = turn_fixture();
    request["phaseTask"] = "读取 weather.json 并返回北京温度。";

    astra::Decision prompt;
    prompt.prompt = "读取 weather.json，然后仅返回结构化结果。";
    record_round(session, logger, request, prompt);

    request["roundNo"] = 2;
    request["llmResp"] = R"({"kind":"command","command":"cat weather.json"})";
    astra::Decision command;
    command.execute_command = "cat weather.json";
    record_round(session, logger, request, command);

    request["roundNo"] = 3;
    request["llmResp"] = "";
    request["lastCmdResult"] = "[exitCode:0]\n{\"北京\":18}";
    astra::Decision review;
    review.prompt = "依据实际结果作答。";
    record_round(session, logger, request, review);

    request["roundNo"] = 4;
    request["lastCmdResult"] = "";
    request["llmResp"] = R"({"kind":"answer","answer":{"北京":18}})";
    astra::Decision answer;
    answer.role_commands[10011].action = "submitAnswer";
    answer.role_commands[10011].task_answer = "{\"北京\":18}";
    record_round(session, logger, request, answer);

    request["roundNo"] = 5;
    request["phaseTask"] = "";
    request["llmResp"] = "";
    request["lastRoundRoleActionResults"]["10011"] = false;
    request["errors"].append(astra::test::parse_json_text(
        R"({"errorCode":2,"description":"答案错误：需要实际观测值"})"));
    request["errors"].append(astra::test::parse_json_text(
        R"({"errorCode":9,"description":""})"));
    record_round(session, logger, request, astra::Decision{});
    record_round(session, logger, request, astra::Decision{});

    const std::vector<Json::Value> events = parse_log_lines(output.str());
    int task_events = 0;
    int error_events = 0;
    bool saw_model_accepted = false;
    bool saw_result_accepted = false;
    bool saw_command = false;
    bool saw_answer = false;
    bool saw_inactive_error_round = false;
    bool saw_duplicate = false;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const Json::Value& event = events[index];
        if (event["event"].asString() == "text") {
            if (event["kind"].asString() == "task") ++task_events;
            if (event["kind"].asString() == "platform_error") {
                ++error_events;
                astra::test::require(event.isMember("error_code"),
                                     "platform error code was not retained");
                if (event["error_code"].asInt() == 2) {
                    astra::test::require(event["content"].asString().find("答案错误") !=
                                             std::string::npos,
                                         "platform error description was not retained");
                }
            }
            continue;
        }
        const Json::Value& task = event["task"];
        saw_model_accepted = saw_model_accepted || task["model_response_accepted"].asBool();
        saw_result_accepted = saw_result_accepted || task["command_result_accepted"].asBool();
        saw_command = saw_command || task["command_emitted"].asBool();
        saw_answer = saw_answer || task["answer_submitted"].asBool();
        saw_duplicate = saw_duplicate || event["request"]["duplicate"].asBool();
        if (event["match"]["round"].asInt() == 5 &&
            !task["active"].asBool() &&
            task["answer_correct"].isNull() &&
            event["platform_error_count"].asInt() == 2 &&
            event["previous_actions"]["illegal"].asInt() == 1) {
            saw_inactive_error_round = true;
        }
    }
    astra::test::require(task_events == 1,
                         "unchanged task text or duplicate request was logged repeatedly");
    astra::test::require(error_events == 2,
                         "duplicate request repeated verbose platform error payload");
    astra::test::require(saw_model_accepted && saw_result_accepted && saw_command && saw_answer,
                         "task prompt/model/command/result/answer pipeline was not observable");
    astra::test::require(saw_inactive_error_round,
                         "final platform error was lost when phaseTask disappeared");
    astra::test::require(saw_duplicate, "repeated request was not marked as a duplicate");
}

ASTRA_TEST(match_log_reemits_same_task_text_after_match_generation_changes) {
    std::ostringstream output;
    astra::MatchLogger logger(output);
    astra::AgentSession session;
    Json::Value request = turn_fixture();
    request["phaseTask"] = "相同题面也属于新比赛";
    record_round(session, logger, request, astra::Decision{});

    request["roundNo"] = 2;
    request["teamOur"]["teamId"] = "astra-new-match";
    record_round(session, logger, request, astra::Decision{});

    const std::vector<Json::Value> events = parse_log_lines(output.str());
    int task_events = 0;
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (events[index]["event"].asString() == "text" &&
            events[index]["kind"].asString() == "task") {
            ++task_events;
        }
    }
    astra::test::require(task_events == 2,
                         "same task text was suppressed across match generations");
}
