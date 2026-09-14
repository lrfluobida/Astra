#!/usr/bin/env python3
import argparse
import copy
import json
import select
import statistics
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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
    elif 600 <= round_no < 610:
        request["phaseTask"] = "合成任务乙"

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

    process = subprocess.Popen(
        [str(Path(args.binary).resolve()), "--replay", "-"],
        cwd=str(ROOT),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    latencies = []
    damaged_rounds = {round_no for round_no in range(257, args.rounds + 1, 257)}
    try:
        for round_no in range(1, args.rounds + 1):
            if round_no in damaged_rounds:
                line = "{damaged json"
            else:
                line = json.dumps(request_for_round(base, round_no), ensure_ascii=False)
            started = time.perf_counter()
            process.stdin.write(line + "\n")
            process.stdin.flush()
            readable, _, _ = select.select([process.stdout], [], [], 2.0)
            if not readable:
                raise AssertionError("no replay response for round {}".format(round_no))
            response_line = process.stdout.readline()
            latencies.append(time.perf_counter() - started)
            response = json.loads(response_line)
            if response != {"roleCommandMap": {}}:
                raise AssertionError(
                    "unexpected response at round {}: {!r}".format(round_no, response)
                )

        process.stdin.close()
        exit_code = process.wait(timeout=5.0)
        if exit_code != 0:
            raise AssertionError("replay process exited with {}".format(exit_code))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3.0)

    maximum = max(latencies)
    p95 = percentile_95(latencies)
    average = statistics.mean(latencies)
    if maximum >= 1.0:
        raise AssertionError("maximum local response latency must stay below 1 second")
    print(
        "PASS replay rounds={} damaged={} avg_ms={:.3f} p95_ms={:.3f} max_ms={:.3f}".format(
            args.rounds,
            len(damaged_rounds),
            average * 1000,
            p95 * 1000,
            maximum * 1000,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
