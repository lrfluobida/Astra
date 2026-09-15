#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-debug}"
build_dir="$project_dir/build"

jsoncpp_root="$project_dir/.tmp/jsoncpp-packages/root/usr"
if [[ -f "$jsoncpp_root/include/jsoncpp/json/json.h" ]]; then
  jsoncpp_flags=(-I"$jsoncpp_root/include/jsoncpp" -L"$jsoncpp_root/lib/x86_64-linux-gnu" \
    -Wl,-rpath,"$jsoncpp_root/lib/x86_64-linux-gnu" -ljsoncpp)
else
  jsoncpp_flags=(-I/usr/include/jsoncpp -ljsoncpp)
fi

common_flags=(-std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc)
case "$mode" in
  debug)
    mode_flags=(-O0 -g)
    ;;
  release)
    mode_flags=(-O2 -DNDEBUG)
    ;;
  sanitize)
    mode_flags=(-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    ;;
  *)
    echo "unknown build mode: $mode" >&2
    exit 2
    ;;
esac

mkdir -p "$build_dir"
cd "$project_dir"
g++ "${common_flags[@]}" "${mode_flags[@]}" \
  src/actions.cpp \
  src/combat.cpp \
  src/defense.cpp \
  src/http_server.cpp \
  src/json_io.cpp \
  src/main.cpp \
  src/navigation.cpp \
  src/protocol.cpp \
  src/session.cpp \
  src/strategy.cpp \
  src/task_solver.cpp \
  -o "$build_dir/astra" \
  "${jsoncpp_flags[@]}"

g++ "${common_flags[@]}" "${mode_flags[@]}" \
  src/actions.cpp \
  src/combat.cpp \
  src/defense.cpp \
  src/json_io.cpp \
  src/navigation.cpp \
  src/protocol.cpp \
  src/session.cpp \
  src/strategy.cpp \
  src/task_solver.cpp \
  tests/test_main.cpp \
  tests/actions_test.cpp \
  tests/combat_test.cpp \
  tests/defense_test.cpp \
  tests/navigation_test.cpp \
  tests/protocol_test.cpp \
  tests/session_test.cpp \
  tests/strategy_test.cpp \
  tests/task_solver_test.cpp \
  -o "$build_dir/astra_tests" \
  "${jsoncpp_flags[@]}"
