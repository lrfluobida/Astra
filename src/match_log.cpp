#include "match_log.hpp"

#include "json_io.hpp"
#include "protocol.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <ostream>
#include <string>

namespace astra {
namespace {

const char* const kSchema = "astra.match_log";
const int kVersion = 1;
const std::size_t kMaximumSummaryUnits = 64;
const std::size_t kMaximumInventoryKinds = 32;
const std::size_t kMaximumActionResults = 64;
const std::size_t kMaximumOutgoingActions = 64;
const std::size_t kMaximumActionTargets = 16;
const std::size_t kMaximumLabelBytes = 128;

std::string utf8_prefix(const std::string& value, std::size_t maximum_bytes) {
    if (value.size() <= maximum_bytes) return value;
    std::size_t end = maximum_bytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) {
        --end;
    }
    return value.substr(0, end);
}

std::size_t utf8_chunk_end(const std::string& value,
                           std::size_t begin,
                           std::size_t maximum_bytes) {
    std::size_t end = std::min(value.size(), begin + maximum_bytes);
    if (end == value.size()) return end;
    while (end > begin && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) {
        --end;
    }
    if (end == begin) return std::min(value.size(), begin + maximum_bytes);
    return end;
}

Json::Value nullable_bool() {
    return Json::Value(Json::nullValue);
}

Json::Value observed_acceptance(const std::string& supplied,
                                const Optional<PendingRequest>& pending_before,
                                const Optional<std::string>& value_before,
                                const Optional<std::string>& value_after) {
    if (supplied.empty()) return nullable_bool();
    if (value_after && *value_after == supplied &&
        (!value_before || *value_before != supplied)) {
        return Json::Value(true);
    }
    if (!pending_before) return Json::Value(false);
    return nullable_bool();
}

Json::Value summarize_unit(const UnitObservation& unit) {
    Json::Value result(Json::objectValue);
    result["id"] = unit.id;
    result["type"] = utf8_prefix(unit.role_type_raw, kMaximumLabelBytes);
    Json::Value position(Json::objectValue);
    position["x"] = unit.pos.x;
    position["y"] = unit.pos.y;
    result["pos"] = std::move(position);
    result["hp"] = unit.health ? Json::Value(*unit.health) : Json::Value(Json::nullValue);
    result["level"] = unit.level ? Json::Value(*unit.level) : Json::Value(Json::nullValue);
    result["inventory_total"] = static_cast<Json::UInt64>(unit.backpack.size());

    std::map<std::string, int> counts;
    for (std::size_t index = 0; index < unit.backpack.size(); ++index) {
        ++counts[unit.backpack[index]];
    }
    Json::Value inventory(Json::arrayValue);
    std::size_t emitted = 0;
    for (std::map<std::string, int>::const_iterator item = counts.begin();
         item != counts.end() && emitted < kMaximumInventoryKinds;
         ++item, ++emitted) {
        Json::Value entry(Json::objectValue);
        entry["name"] = utf8_prefix(item->first, kMaximumLabelBytes);
        entry["count"] = item->second;
        inventory.append(std::move(entry));
    }
    result["inventory"] = std::move(inventory);
    result["inventory_truncated"] = counts.size() > emitted;
    return result;
}

bool nonempty_string(const Json::Value& object, const char* key) {
    return object.isObject() && object.isMember(key) && object[key].isString() &&
           !object[key].asString().empty();
}

bool find_submitted_answer(const Json::Value& response) {
    if (!response.isObject() || !response["roleCommandMap"].isObject()) return false;
    const Json::Value& commands = response["roleCommandMap"];
    const std::vector<std::string> actors = commands.getMemberNames();
    for (std::size_t index = 0; index < actors.size(); ++index) {
        const Json::Value& command = commands[actors[index]];
        if (command["action"].isString() && command["action"].asString() == "submitAnswer" &&
            nonempty_string(command, "taskAnswer")) {
            return true;
        }
    }
    return false;
}

Json::Value summarize_outgoing_actions(const Json::Value& response, bool& truncated) {
    Json::Value result(Json::arrayValue);
    truncated = false;
    if (!response.isObject() || !response["roleCommandMap"].isObject()) return result;
    const Json::Value& commands = response["roleCommandMap"];
    const std::vector<std::string> actors = commands.getMemberNames();
    for (std::size_t index = 0;
         index < actors.size() && index < kMaximumOutgoingActions;
         ++index) {
        const Json::Value& command = commands[actors[index]];
        Json::Value entry(Json::objectValue);
        entry["actor"] = actors[index];
        if (command["action"].isString()) entry["action"] = command["action"].asString();
        if (command["controllerId"].isString()) {
            entry["controller"] = command["controllerId"].asString();
        }
        if (command["name"].isString()) {
            entry["name"] = utf8_prefix(command["name"].asString(), kMaximumLabelBytes);
        }
        if (command["num"].isInt()) entry["num"] = command["num"].asInt();
        if (command["targetPos"].isArray()) {
            entry["target_count"] = static_cast<Json::UInt64>(command["targetPos"].size());
            Json::Value targets(Json::arrayValue);
            const Json::ArrayIndex target_count = std::min<Json::ArrayIndex>(
                command["targetPos"].size(),
                static_cast<Json::ArrayIndex>(kMaximumActionTargets));
            for (Json::ArrayIndex target_index = 0;
                 target_index < target_count;
                 ++target_index) {
                targets.append(command["targetPos"][target_index]);
            }
            entry["targets"] = std::move(targets);
            entry["targets_truncated"] = command["targetPos"].size() > target_count;
        }
        result.append(std::move(entry));
    }
    truncated = actors.size() > kMaximumOutgoingActions;
    return result;
}

void add_common_fields(Json::Value& event, std::uint64_t sequence) {
    event["schema"] = kSchema;
    event["version"] = kVersion;
    event["seq"] = static_cast<Json::UInt64>(sequence);
}

}  // namespace

MatchLogger::MatchLogger(std::ostream& output,
                         std::size_t chunk_bytes,
                         std::size_t maximum_payload_bytes)
    : output_(output),
      chunk_bytes_(std::max<std::size_t>(4, chunk_bytes)),
      maximum_payload_bytes_(std::max<std::size_t>(4, maximum_payload_bytes)) {}

MatchLogger::Fingerprint MatchLogger::fingerprint(const std::string& value) const {
    Fingerprint result;
    result.first = 1469598103934665603ULL;
    result.second = 1099511628211ULL;
    result.bytes = value.size();
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::uint64_t byte = static_cast<unsigned char>(value[index]);
        result.first ^= byte;
        result.first *= 1099511628211ULL;
        result.second ^= byte + 0x9e3779b97f4a7c15ULL + (result.second << 6) +
                         (result.second >> 2);
    }
    return result;
}

bool MatchLogger::same_fingerprint(const Fingerprint& left,
                                   const Fingerprint& right) const {
    return left.first == right.first && left.second == right.second &&
           left.bytes == right.bytes;
}

void MatchLogger::emit(Json::Value event) {
    add_common_fields(event, ++sequence_);
    output_ << "ASTRA_LOG " << write_json(event) << '\n';
}

void MatchLogger::emit_text(std::uint64_t parent_sequence,
                            int ordinal,
                            const std::string& kind,
                            const std::string& content,
                            const Json::Value& metadata) {
    if (content.empty()) return;
    const std::size_t logged_bytes = std::min(content.size(), maximum_payload_bytes_);
    const std::string bounded = utf8_prefix(content, logged_bytes);
    std::size_t chunk_count = 0;
    for (std::size_t begin = 0; begin < bounded.size();) {
        begin = utf8_chunk_end(bounded, begin, chunk_bytes_);
        ++chunk_count;
    }
    if (chunk_count == 0) chunk_count = 1;

    const std::string event_id = std::to_string(parent_sequence) + ":" +
                                 std::to_string(ordinal) + ":" + kind;
    std::size_t chunk_index = 0;
    for (std::size_t begin = 0; begin < bounded.size();) {
        const std::size_t end = utf8_chunk_end(bounded, begin, chunk_bytes_);
        Json::Value event = metadata;
        if (!event.isObject()) event = Json::Value(Json::objectValue);
        event["event"] = "text";
        event["parent_seq"] = static_cast<Json::UInt64>(parent_sequence);
        event["event_id"] = event_id;
        event["kind"] = kind;
        event["chunk_index"] = static_cast<Json::UInt64>(chunk_index);
        event["chunk_count"] = static_cast<Json::UInt64>(chunk_count);
        event["original_bytes"] = static_cast<Json::UInt64>(content.size());
        event["logged_bytes"] = static_cast<Json::UInt64>(bounded.size());
        event["truncated"] = bounded.size() < content.size();
        event["content"] = bounded.substr(begin, end - begin);
        emit(std::move(event));
        begin = end;
        ++chunk_index;
    }
}

void MatchLogger::log_round(const Json::Value& request,
                            const Json::Value& response,
                            const SessionDiagnostics& before,
                            const SessionDiagnostics& after,
                            std::chrono::microseconds elapsed) {
    const ParseResult parsed = parse_turn(request);
    const bool schema_valid = parsed.turn && parsed.errors.empty();
    const int round = request.isObject() && request["roundNo"].isInt()
                          ? request["roundNo"].asInt()
                          : 0;
    const Fingerprint request_fingerprint = fingerprint(write_json(request));
    const bool duplicate = has_last_request_ && last_generation_ == after.generation &&
                           last_round_ == round &&
                           same_fingerprint(last_request_, request_fingerprint);

    Json::Value summary(Json::objectValue);
    summary["event"] = "round";
    Json::Value request_summary(Json::objectValue);
    request_summary["parse_status"] = schema_valid ? "ok" : "invalid_schema";
    request_summary["duplicate"] = duplicate;
    request_summary["schema_error_count"] =
        static_cast<Json::UInt64>(parsed.errors.size());
    summary["request"] = std::move(request_summary);

    Json::Value match(Json::objectValue);
    match["generation"] = static_cast<Json::UInt64>(after.generation);
    match["round"] = round;
    match["day"] = round > 0 ? (round - 1) / 130 + 1 : 0;
    match["team_id"] = Json::Value(Json::nullValue);
    match["team_type"] = Json::Value(Json::nullValue);

    Json::Value economy(Json::objectValue);
    economy["gold"] = Json::Value(Json::nullValue);
    economy["score"] = Json::Value(Json::nullValue);
    Json::Value units(Json::arrayValue);
    bool units_truncated = false;
    int own_robots = 0;
    int enemy_robots = 0;
    int unknown_robots = 0;
    if (schema_valid) {
        const TurnObservation& turn = *parsed.turn;
        match["team_id"] = turn.team_our.team_id;
        match["team_type"] = turn.team_our.type;
        economy["gold"] = turn.team_our.gold;
        economy["score"] = turn.team_our.total_score;
        const std::size_t unit_count =
            std::min(turn.team_our.roles.size(), kMaximumSummaryUnits);
        for (std::size_t index = 0; index < unit_count; ++index) {
            units.append(summarize_unit(turn.team_our.roles[index]));
        }
        units_truncated = turn.team_our.roles.size() > unit_count;
        for (std::size_t index = 0; index < turn.robots.size(); ++index) {
            const RobotObservation& robot = turn.robots[index];
            if (robot.health <= 0) continue;
            if (!robot.target_team || robot.target_team->empty()) {
                ++unknown_robots;
            } else if ((turn.team_our.type == "challenger" &&
                        *robot.target_team == "defender") ||
                       (turn.team_our.type == "defender" &&
                        *robot.target_team == "challenger")) {
                ++enemy_robots;
            } else if (*robot.target_team == turn.team_our.type ||
                       *robot.target_team == turn.team_our.team_id) {
                ++own_robots;
            } else {
                ++unknown_robots;
            }
        }
    }
    summary["match"] = std::move(match);
    summary["economy"] = std::move(economy);
    summary["units"] = std::move(units);
    summary["units_truncated"] = units_truncated;
    Json::Value robots(Json::objectValue);
    robots["targeting_our_team"] = own_robots;
    robots["targeting_opponent"] = enemy_robots;
    robots["unknown"] = unknown_robots;
    summary["robots"] = std::move(robots);

    Json::Value previous_actions(Json::objectValue);
    Json::Value action_results(Json::objectValue);
    int known_actions = 0;
    int legal_actions = 0;
    int illegal_actions = 0;
    bool action_results_truncated = false;
    if (request.isObject() && request["lastRoundRoleActionResults"].isObject()) {
        const Json::Value& raw_results = request["lastRoundRoleActionResults"];
        const std::vector<std::string> actors = raw_results.getMemberNames();
        for (std::size_t index = 0; index < actors.size(); ++index) {
            if (!raw_results[actors[index]].isBool()) continue;
            const bool legal = raw_results[actors[index]].asBool();
            ++known_actions;
            if (legal) ++legal_actions;
            else ++illegal_actions;
            if (index < kMaximumActionResults) action_results[actors[index]] = legal;
        }
        action_results_truncated = actors.size() > kMaximumActionResults;
    }
    previous_actions["known"] = known_actions;
    previous_actions["legal"] = legal_actions;
    previous_actions["illegal"] = illegal_actions;
    previous_actions["results"] = std::move(action_results);
    previous_actions["results_truncated"] = action_results_truncated;
    summary["previous_actions"] = std::move(previous_actions);

    bool outgoing_truncated = false;
    summary["outgoing_actions"] = summarize_outgoing_actions(response, outgoing_truncated);
    summary["outgoing_actions_truncated"] = outgoing_truncated;

    const std::string task_text = nonempty_string(request, "phaseTask")
                                      ? request["phaseTask"].asString()
                                      : std::string();
    const std::string model_response = nonempty_string(request, "llmResp")
                                           ? request["llmResp"].asString()
                                           : std::string();
    const std::string command_result = nonempty_string(request, "lastCmdResult")
                                           ? request["lastCmdResult"].asString()
                                           : std::string();
    const bool prompt_emitted = nonempty_string(response, "prompt");
    const bool command_emitted = nonempty_string(response, "executeCmd");
    const bool answer_submitted = find_submitted_answer(response);
    bool task_changed = false;
    Fingerprint task_fingerprint;
    if (!task_text.empty()) {
        task_fingerprint = fingerprint(task_text);
        task_changed = !has_last_task_ || last_task_generation_ != after.generation ||
                       !same_fingerprint(last_task_, task_fingerprint);
    }

    Json::Value task(Json::objectValue);
    task["serial"] = after.task_serial;
    task["active"] = !task_text.empty();
    task["text_changed"] = task_changed;
    task["prompt_emitted"] = prompt_emitted;
    task["model_response_received"] = !model_response.empty();
    task["model_response_accepted"] =
        observed_acceptance(model_response,
                            before.pending_prompt,
                            before.last_llm_result,
                            after.last_llm_result);
    task["command_emitted"] = command_emitted;
    task["command_result_received"] = !command_result.empty();
    task["command_result_accepted"] =
        observed_acceptance(command_result,
                            before.pending_command,
                            before.last_command_result,
                            after.last_command_result);
    task["answer_submitted"] = answer_submitted;
    task["answer_correct"] = nullable_bool();
    summary["task"] = std::move(task);
    summary["platform_error_count"] = request.isObject() && request["errors"].isArray()
                                                  ? request["errors"].size()
                                                  : 0;
    summary["elapsed_us"] = static_cast<Json::Int64>(elapsed.count());
    summary["session_degraded"] = !after.degradation_reason.empty();

    add_common_fields(summary, sequence_ + 1);
    std::string serialized_summary = write_json(summary);
    if (serialized_summary.size() > maximum_payload_bytes_) {
        summary["units"] = Json::Value(Json::arrayValue);
        summary["outgoing_actions"] = Json::Value(Json::arrayValue);
        summary["previous_actions"]["results"] = Json::Value(Json::objectValue);
        summary["summary_truncated"] = true;
    }
    const std::uint64_t parent_sequence = sequence_ + 1;
    emit(std::move(summary));

    int text_ordinal = 0;
    if (!duplicate) {
        if (task_changed) {
            emit_text(parent_sequence, ++text_ordinal, "task", task_text);
        }
        emit_text(parent_sequence, ++text_ordinal, "model_response", model_response);
        if (prompt_emitted) {
            emit_text(parent_sequence, ++text_ordinal, "prompt", response["prompt"].asString());
        }
        if (command_emitted) {
            emit_text(parent_sequence,
                      ++text_ordinal,
                      "execute_command",
                      response["executeCmd"].asString());
        }
        emit_text(parent_sequence, ++text_ordinal, "command_result", command_result);
        if (response.isObject() && response["roleCommandMap"].isObject()) {
            const Json::Value& commands = response["roleCommandMap"];
            const std::vector<std::string> actors = commands.getMemberNames();
            for (std::size_t index = 0; index < actors.size(); ++index) {
                const Json::Value& command = commands[actors[index]];
                if (!nonempty_string(command, "taskAnswer")) continue;
                Json::Value metadata(Json::objectValue);
                metadata["actor"] = actors[index];
                emit_text(parent_sequence,
                          ++text_ordinal,
                          "answer",
                          command["taskAnswer"].asString(),
                          metadata);
            }
        }
        if (request.isObject() && request["errors"].isArray()) {
            const Json::Value& errors = request["errors"];
            for (Json::ArrayIndex index = 0; index < errors.size(); ++index) {
                const Json::Value& error = errors[index];
                if (!error.isObject() || !error["description"].isString()) {
                    continue;
                }
                Json::Value metadata(Json::objectValue);
                if (error["errorCode"].isInt()) metadata["error_code"] = error["errorCode"];
                const std::string description = error["description"].asString().empty()
                                                    ? "<empty description>"
                                                    : error["description"].asString();
                emit_text(parent_sequence,
                          ++text_ordinal,
                          "platform_error",
                          description,
                          metadata);
            }
        }
        for (std::size_t index = 0; index < parsed.errors.size(); ++index) {
            emit_text(parent_sequence,
                      ++text_ordinal,
                      "schema_error",
                      parsed.errors[index]);
        }
        emit_text(parent_sequence,
                  ++text_ordinal,
                  "session_error",
                  after.degradation_reason);
    }

    has_last_request_ = true;
    last_request_ = request_fingerprint;
    last_generation_ = after.generation;
    last_round_ = round;
    if (task_text.empty()) {
        has_last_task_ = false;
    } else {
        has_last_task_ = true;
        last_task_ = task_fingerprint;
        last_task_generation_ = after.generation;
    }
}

}  // namespace astra
