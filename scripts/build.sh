#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-debug}"
build_dir="$project_dir/build"

common_flags=(-std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc -Ithird_party)
case "$mode" in
  debug)
    mode_flags=(-O0 -g)
    ;;
  release)
    mode_flags=(-O2 -DNDEBUG)
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
  src/main.cpp \
  src/protocol.cpp \
  src/session.cpp \
  -o "$build_dir/astra"

g++ "${common_flags[@]}" "${mode_flags[@]}" \
  src/actions.cpp \
  src/protocol.cpp \
  src/session.cpp \
  tests/test_main.cpp \
  tests/actions_test.cpp \
  tests/protocol_test.cpp \
  tests/session_test.cpp \
  -o "$build_dir/astra_tests"
