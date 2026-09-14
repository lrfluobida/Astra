#include "task_solver.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace astra {
namespace {

constexpr std::size_t kMaximumCommandLength = 32768;

const UnitObservation* living_pioneer(const TurnObservation& turn) {
    for (const auto& unit : turn.team_our.roles) {
        if (unit.owned && unit.role_type == RoleType::pioneer && unit.health &&
            *unit.health > 0) {
            return &unit;
        }
    }
    return nullptr;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                          return std::isspace(ch) != 0;
                      }).base();
    if (first >= last) return "";
    return std::string(first, last);
}

std::string error_context(const TurnObservation& turn) {
    if (turn.errors.empty()) return "无。";
    std::ostringstream result;
    for (const auto& error : turn.errors) {
        result << "code=" << error.code << ": " << error.description << '\n';
    }
    return result.str();
}

std::string protocol_text() {
    return
        "只输出一个单行 JSON 对象，不要 Markdown、解释或额外文字。\n"
        "可以直接作答时：{\"kind\":\"answer\",\"answer\":\"最终提交内容\"}\n"
        "必须读取文件或计算时：{\"kind\":\"command\",\"command\":\"一条可执行的 shell 命令\"}\n"
        "answer 可以是字符串、对象或数组；必须严格保留题目要求的字段名和格式。\n"
        "command 必须在 10 秒内结束，输出仅保留下一步求解所需证据，不启动后台进程。";
}

std::string initial_prompt(const TurnObservation& turn) {
    return
        "你是 Astra 的高精度离线任务求解器。先在内部逐项提取目标、约束、答案字段和可验证证据，再选择直接回答或执行一次命令。"
        "沙盒无法访问外部网络，但可运行基础 shell 和 Python；不得假设不存在的文件、API、字段或数据。\n\n"
        "<TASK>\n" +
        turn.phase_task +
        "\n</TASK>\n\n"
        "<PREVIOUS_ERRORS>\n" +
        error_context(turn) + "</PREVIOUS_ERRORS>\n\n" + protocol_text();
}

std::string review_prompt(const TurnObservation& turn) {
    return
        "你是 Astra 的结果审查器。根据原始任务和真实沙盒输出逐字段核验。若证据足够，给出最终答案；若仍缺少必要证据，只能再给一条最小命令。"
        "不要编造命令未输出的信息。\n\n"
        "<TASK>\n" +
        turn.phase_task +
        "\n</TASK>\n\n"
        "<COMMAND_RESULT>\n" +
        turn.last_command_result +
        "\n</COMMAND_RESULT>\n\n"
        "<PREVIOUS_ERRORS>\n" +
        error_context(turn) + "</PREVIOUS_ERRORS>\n\n" + protocol_text();
}

std::string repair_prompt(const TurnObservation& turn, const std::string& response) {
    return
        "格式修复：你上一次输出像 JSON，但无法解析或缺少必需字段。不要重新讲解，只根据原任务修复输出格式。\n\n"
        "<TASK>\n" +
        turn.phase_task +
        "\n</TASK>\n\n"
        "<INVALID_OUTPUT>\n" +
        response + "\n</INVALID_OUTPUT>\n\n" + protocol_text();
}

enum class ModelResultKind {
    answer,
    command,
    malformed,
};

struct ModelResult {
    ModelResultKind kind = ModelResultKind::malformed;
    std::string value;
};

std::string answer_text(const nlohmann::json& answer) {
    return answer.is_string() ? answer.get<std::string>() : answer.dump();
}

ModelResult parse_model_result(const std::string& raw) {
    const std::string response = trim(raw);
    const auto first_brace = response.find('{');
    const auto last_brace = response.rfind('}');
    if (first_brace == std::string::npos && last_brace == std::string::npos) {
        return {ModelResultKind::answer, response};
    }
    if (first_brace == std::string::npos || last_brace == std::string::npos ||
        first_brace > last_brace) {
        return {ModelResultKind::malformed, response};
    }

    try {
        const auto parsed =
            nlohmann::json::parse(response.substr(first_brace, last_brace - first_brace + 1));
        if (!parsed.is_object()) return {ModelResultKind::malformed, response};
        const std::string kind = parsed.value("kind", "");
        if ((kind == "answer" || kind.empty()) && parsed.contains("answer") &&
            !parsed["answer"].is_null()) {
            const std::string answer = trim(answer_text(parsed["answer"]));
            return answer.empty() ? ModelResult{ModelResultKind::malformed, response}
                                  : ModelResult{ModelResultKind::answer, answer};
        }
        if ((kind == "command" || kind.empty()) && parsed.contains("command") &&
            parsed["command"].is_string()) {
            const std::string command = trim(parsed["command"].get<std::string>());
            if (!command.empty() && command.size() <= kMaximumCommandLength &&
                command.find('\0') == std::string::npos) {
                return {ModelResultKind::command, command};
            }
        }
    } catch (const nlohmann::json::exception&) {
    }
    return {ModelResultKind::malformed, response};
}

TopLevelCandidate prompt_candidate(std::string prompt, int priority, int pioneer_id) {
    TopLevelCandidate candidate;
    candidate.kind = TopLevelKind::prompt;
    candidate.value = std::move(prompt);
    candidate.priority = priority;
    candidate.requires_active_task = true;
    candidate.pioneer_id = pioneer_id;
    return candidate;
}

TopLevelCandidate command_candidate(std::string command, int priority, int pioneer_id) {
    TopLevelCandidate candidate;
    candidate.kind = TopLevelKind::execute_command;
    candidate.value = std::move(command);
    candidate.priority = priority;
    candidate.requires_active_task = true;
    candidate.pioneer_id = pioneer_id;
    return candidate;
}

CandidateAction answer_candidate(int pioneer_id, std::string answer, int priority) {
    CandidateAction candidate;
    candidate.action_key = pioneer_id;
    candidate.command.action = "submitAnswer";
    candidate.command.task_answer = std::move(answer);
    candidate.priority = priority;
    candidate.source = "task.submit";
    return candidate;
}

}  // namespace

TaskCandidates task_candidates(const TurnObservation& turn, int priority) {
    TaskCandidates result;
    const auto* pioneer = living_pioneer(turn);
    if (turn.phase_task.empty() || !pioneer) return result;

    result.rules.task_active = true;
    result.rules.pioneer_id = pioneer->id;
    if (!turn.last_command_result.empty()) {
        result.top_level.push_back(prompt_candidate(review_prompt(turn), priority, pioneer->id));
        return result;
    }
    if (turn.llm_response.empty()) {
        result.top_level.push_back(prompt_candidate(initial_prompt(turn), priority, pioneer->id));
        return result;
    }

    const ModelResult parsed = parse_model_result(turn.llm_response);
    if (parsed.kind == ModelResultKind::answer) {
        result.actions.push_back(answer_candidate(pioneer->id, parsed.value, priority));
    } else if (parsed.kind == ModelResultKind::command) {
        result.top_level.push_back(command_candidate(parsed.value, priority, pioneer->id));
    } else {
        result.top_level.push_back(
            prompt_candidate(repair_prompt(turn, turn.llm_response), priority, pioneer->id));
    }
    return result;
}

}  // namespace astra
