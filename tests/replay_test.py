#!/usr/bin/env python3
import argparse
import copy
import json
import select
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SUMMON_ORDERS = {
    "SmallRobotSummonOrder", "MiddleRobotSummonOrder",
    "LargeRobotSummonOrder", "BossRobotSummonOrder",
}
ALLOWED_ACTIONS = {
    "move",
    "collect",
    "sell",
    "acceptTask",
    "attack",
    "submitAnswer",
    "build",
    "buy",
    "use",
}


def validate_response(response, round_no):
    commands = response.get("roleCommandMap")
    if not isinstance(commands, dict):
        raise AssertionError("round {} has no roleCommandMap object".format(round_no))
    move_targets = set()
    controllers = set()
    for actor_id, command in commands.items():
        action = command.get("action")
        if action not in ALLOWED_ACTIONS:
            raise AssertionError(
                "round {} actor {} has invalid action {!r}".format(
                    round_no, actor_id, action
                )
            )
        if action in {"move", "collect", "build"} or (
            action == "use" and command.get("name") not in SUMMON_ORDERS
        ):
            targets = command.get("targetPos")
            if not isinstance(targets, list) or len(targets) != 1:
                raise AssertionError("{} requires one targetPos".format(action))
            target = targets[0]
            if not isinstance(target.get("x"), int) or not isinstance(target.get("y"), int):
                raise AssertionError("targetPos must contain integer coordinates")
            if action == "move":
                key = (target["x"], target["y"])
                if key in move_targets:
                    raise AssertionError("round {} contains conflicting moves".format(round_no))
                move_targets.add(key)
        if action == "sell":
            if not isinstance(command.get("name"), str) or command.get("num", 0) <= 0:
                raise AssertionError("sell requires a name and positive num")
        if action in {"build", "buy", "use"} and not isinstance(command.get("name"), str):
            raise AssertionError("{} requires a name".format(action))
        if action == "buy" and command.get("num", 0) <= 0:
            raise AssertionError("buy requires a positive num")
        if action == "attack":
            controller = command.get("controllerId")
            targets = command.get("targetPos")
            if not isinstance(controller, str) or not isinstance(targets, list) or len(targets) != 1:
                raise AssertionError("level one attack requires controllerId and one targetPos")
            controllers.add(controller)
        if action == "submitAnswer" and not isinstance(command.get("taskAnswer"), str):
            raise AssertionError("submitAnswer requires a string taskAnswer")
    if len(controllers) != sum(
        command.get("action") == "attack" for command in commands.values()
    ):
        raise AssertionError("one controller was assigned to multiple weapons")
    if controllers.intersection(commands):
        raise AssertionError("weapon controller also received a character action")


def request_for_round(base, round_no):
    request = copy.deepcopy(base)
    request["roundNo"] = round_no
    day = (round_no - 1) // 130 + 1
    request["worldNews"]["officialNews"] = "第{}天：Astra 保持观察".format(day)
    request["worldNews"]["folkLegends"] = "第{}回合的合成传闻".format(round_no)

    if 300 <= round_no < 320:
        request["teamOur"]["roles"] = [
            role for role in request["teamOur"]["roles"] if role["id"] != 10010
        ]
    if 400 <= round_no < 410:
        request["phaseTask"] = "合成任务甲"
        if round_no == 401:
            request["llmResp"] = (
                '{"kind":"command","command":"python3 -c \'print(42)\'"}'
            )
        elif round_no == 402:
            request["lastCmdResult"] = "[exitCode:0]\n42"
        elif round_no == 403:
            request["llmResp"] = '{"kind":"answer","answer":{"value":42}}'
    elif 600 <= round_no < 610:
        request["phaseTask"] = "合成任务乙"
        if round_no == 601:
            request["llmResp"] = "直接答案"

    for role in request["teamOur"]["roles"]:
        role.pop("cooldown", None)
    return request


def percentile_95(values):
    ordered = sorted(values)
    return ordered[max(0, int(len(ordered) * 0.95) - 1)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--rounds", type=int, default=1300)
    args = parser.parse_args()
    if args.rounds <= 0:
        raise SystemExit("--rounds must be positive")

    with (ROOT / "tests" / "fixtures" / "minimal_turn.json").open(
        "r", encoding="utf-8"
    ) as source:
        base = json.load(source)
    base["mapInfo"]["zones"].extend(
        [
            {"neutralType": "copper", "pos": {"x": 8, "y": 25}},
            {"neutralType": "iron", "pos": {"x": 7, "y": 28}},
            {"neutralType": "vendor", "pos": {"x": 20, "y": 16}},
        ]
    )
    base["vendorShopList"] = [
        {"name": "copper", "price": 5},
        {"name": "iron", "price": 8},
    ]
    base["teamOur"]["roles"].append(
        {
            "id": 10020,
            "pos": {"x": 8, "y": 24},
            "roleType": "gatling",
            "health": 1000,
            "attackPower": 10,
            "attackRange": 5,
            "backPackCapability": 0,
            "backpack": [],
            "level": 1,
            "cooldown": 0,
        }
    )
    base["robot"]["roles"] = [
        {
            "id": 30001,
            "pos": {"x": 12, "y": 24},
            "roleType": "smallRobot",
            "health": 40,
            "abnormalState": "",
            "targetTeam": "challenger",
        }
    ]

    stderr_file = tempfile.TemporaryFile(mode="w+b")
    process = subprocess.Popen(
        [str(Path(args.binary).resolve()), "--replay", "-"],
        cwd=str(ROOT),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=stderr_file,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    latencies = []
    damaged_rounds = {round_no for round_no in range(257, args.rounds + 1, 257)}
    repeated_requests = 0
    attack_responses = 0
    prompt_responses = 0
    command_responses = 0
    submit_responses = 0

    def exchange(line, round_no):
        started = time.perf_counter()
        process.stdin.write(line + "\n")
        process.stdin.flush()
        readable, _, _ = select.select([process.stdout], [], [], 2.0)
        if not readable:
            raise AssertionError("no replay response for round {}".format(round_no))
        response_line = process.stdout.readline()
        latencies.append(time.perf_counter() - started)
        return json.loads(response_line)

    try:
        for round_no in range(1, args.rounds + 1):
            if round_no in damaged_rounds:
                line = "{damaged json"
            else:
                line = json.dumps(request_for_round(base, round_no), ensure_ascii=False)
            response = exchange(line, round_no)
            if round_no in damaged_rounds:
                if response != {"roleCommandMap": {}}:
                    raise AssertionError(
                        "damaged request must be conservative at round {}".format(round_no)
                    )
            else:
                validate_response(response, round_no)
                if "prompt" in response and "executeCmd" in response:
                    raise AssertionError("one response must not request prompt and executeCmd together")
                if "prompt" in response:
                    if not isinstance(response["prompt"], str) or not response["prompt"]:
                        raise AssertionError("prompt must be a non-empty string")
                    prompt_responses += 1
                if "executeCmd" in response:
                    if not isinstance(response["executeCmd"], str) or not response["executeCmd"]:
                        raise AssertionError("executeCmd must be a non-empty string")
                    command_responses += 1
                submit_responses += sum(
                    command.get("action") == "submitAnswer"
                    for command in response["roleCommandMap"].values()
                )
                attack_responses += sum(
                    command.get("action") == "attack"
                    for command in response["roleCommandMap"].values()
                )

            if round_no == min(200, args.rounds) and round_no not in damaged_rounds:
                repeated = exchange(line, round_no)
                repeated_requests += 1
                if repeated != response:
                    raise AssertionError(
                        "identical replay request changed at round {}".format(round_no)
                    )
            if round_no not in damaged_rounds and round_no <= 70 and not response["roleCommandMap"]:
                raise AssertionError(
                    "daytime strategy unexpectedly returned empty at round {}".format(round_no)
                )

        process.stdin.close()
        exit_code = process.wait(timeout=5.0)
        if exit_code != 0:
            raise AssertionError("replay process exited with {}".format(exit_code))
        stderr_file.seek(0)
        stderr_lines = stderr_file.read().decode("utf-8").splitlines()
        round_logs = []
        for line in stderr_lines:
            if not line.startswith("ASTRA_LOG "):
                continue
            event = json.loads(line[len("ASTRA_LOG "):])
            if event.get("event") == "round":
                round_logs.append(event)
        expected_round_logs = args.rounds - len(damaged_rounds) + repeated_requests
        if len(round_logs) != expected_round_logs:
            raise AssertionError(
                "expected {} structured round logs, found {}".format(
                    expected_round_logs, len(round_logs)
                )
            )
        if not any(event.get("request", {}).get("duplicate") for event in round_logs):
            raise AssertionError("repeated replay request was not marked duplicate in stderr")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3.0)
        stderr_file.close()

    maximum = max(latencies)
    p95 = percentile_95(latencies)
    average = statistics.mean(latencies)
    if maximum >= 1.0:
        raise AssertionError("maximum local response latency must stay below 1 second")
    if args.rounds >= 71 and attack_responses == 0:
        raise AssertionError("night replay never exercised the combat strategy")
    if args.rounds >= 403 and (prompt_responses == 0 or command_responses == 0 or submit_responses == 0):
        raise AssertionError("replay did not exercise the complete task prompt pipeline")
    print(
        "PASS replay rounds={} repeated={} damaged={} avg_ms={:.3f} p95_ms={:.3f} max_ms={:.3f}".format(
            args.rounds,
            repeated_requests,
            len(damaged_rounds),
            average * 1000,
            p95 * 1000,
            maximum * 1000,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
