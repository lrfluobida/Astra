#!/usr/bin/env python3
import argparse
import copy
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def post(port, path, body):
    request = urllib.request.Request(
        "http://127.0.0.1:{}{}".format(port, path),
        data=body,
        headers={"Content-Type": "application/json; charset=utf-8"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=2.0) as response:
        if response.status != 200:
            raise AssertionError("unexpected HTTP status: {}".format(response.status))
        return json.loads(response.read().decode("utf-8"))


def wait_until_ready(process, port):
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError("server exited before becoming ready")
        try:
            post(port, "/", b"not-json")
            return
        except (OSError, urllib.error.URLError):
            time.sleep(0.05)
    raise AssertionError("server did not become ready within 5 seconds")


def run_server(command, fixture):
    fixture["mapInfo"]["zones"].append(
        {"neutralType": "copper", "pos": {"x": 8, "y": 25}}
    )
    fixture["vendorShopList"] = [{"name": "copper", "price": 5}]
    port = reserve_port()
    process = subprocess.Popen(
        command(port),
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_until_ready(process, port)

        first = post(
            port,
            "/",
            json.dumps(fixture, ensure_ascii=False).encode("utf-8"),
        )
        worker_command = first.get("roleCommandMap", {}).get("10010")
        if worker_command != {
            "action": "collect",
            "targetPos": [{"x": 8, "y": 25}],
        }:
            raise AssertionError("strategy was not used by HTTP entrypoint: {!r}".format(first))

        repeated = post(
            port,
            "/",
            json.dumps(fixture, ensure_ascii=False).encode("utf-8"),
        )
        if repeated != first:
            raise AssertionError("identical request must return the cached response")

        fixture["roundNo"] = 2
        fixture["worldNews"]["officialNews"] = "第二回合仍能处理中文"
        second = post(
            port,
            "/act",
            json.dumps(fixture, ensure_ascii=False).encode("utf-8"),
        )
        if second.get("roleCommandMap", {}).get("10010", {}).get("action") != "collect":
            raise AssertionError("strategy did not advance on the second round: {!r}".format(second))

        invalid = post(port, "/act", b"{invalid json")
        if invalid != {"roleCommandMap": {}}:
            raise AssertionError("invalid JSON must receive a conservative response")

        fixture["roundNo"] = 3
        missing_position = copy.deepcopy(fixture)
        del missing_position["teamOur"]["roles"][1]["pos"]
        incomplete = post(
            port,
            "/act",
            json.dumps(missing_position, ensure_ascii=False).encode("utf-8"),
        )
        if incomplete != {"roleCommandMap": {}}:
            raise AssertionError("missing required fields must receive a conservative response")

        recovered = post(
            port,
            "/act",
            json.dumps(fixture, ensure_ascii=False).encode("utf-8"),
        )
        if recovered.get("roleCommandMap", {}).get("10010", {}).get("action") != "collect":
            raise AssertionError("server did not recover after invalid JSON")

        night = copy.deepcopy(fixture)
        night["roundNo"] = 71
        night_positions = {
            10010: {"x": 1, "y": 1},
            10012: {"x": 1, "y": 5},
            10011: {"x": 5, "y": 1},
            10013: {"x": 10, "y": 10},
        }
        for role in night["teamOur"]["roles"]:
            role["pos"] = night_positions[role["id"]]
        night_response = post(
            port,
            "/act",
            json.dumps(night, ensure_ascii=False).encode("utf-8"),
        )
        night_commands = night_response.get("roleCommandMap", {})
        targets = []
        for actor_id in ("10010", "10012", "10011"):
            command = night_commands.get(actor_id)
            if not command or command.get("action") != "move":
                raise AssertionError("night return omitted actor {}".format(actor_id))
            target = command.get("targetPos", [{}])[0]
            targets.append((target.get("x"), target.get("y")))
        if len(set(targets)) != len(targets):
            raise AssertionError("arbitration allowed conflicting move destinations")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    with (ROOT / "tests" / "fixtures" / "minimal_turn.json").open(
        "r", encoding="utf-8"
    ) as source:
        fixture = json.load(source)

    run_server(lambda port: [binary, str(port)], copy.deepcopy(fixture))
    run_server(lambda port: ["bash", "run.sh", str(port)], copy.deepcopy(fixture))
    print("PASS http server: direct binary and run.sh")
    return 0


if __name__ == "__main__":
    sys.exit(main())
