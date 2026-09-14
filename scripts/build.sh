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
  tests/test_main.cpp \
  -o "$build_dir/astra_tests"
