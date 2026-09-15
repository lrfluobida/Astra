#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-debug}"

cd "$project_dir"
bash scripts/build.sh "$mode"
./build/astra_tests
python3 tests/http_test.py --binary ./build/astra
python3 tests/replay_test.py --binary ./build/astra --rounds 1300
python3 tests/package_test.py
