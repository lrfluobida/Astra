#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -ne 1 ]]; then
  echo "usage: bash run.sh <port>" >&2
  exit 2
fi

exec "$project_dir/build/astra" "$1"
