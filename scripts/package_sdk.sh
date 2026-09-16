#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist_dir="$project_dir/dist"
staging_dir="$dist_dir/CoreGeek"
archive="$dist_dir/Astra-CoreGeek.tar.gz"

case "$dist_dir" in
  "$project_dir"/dist) ;;
  *) echo "refusing unsafe dist path: $dist_dir" >&2; exit 2 ;;
esac

if grep -R -n -E 'nlohmann|httplib' "$project_dir/src" "$project_dir/CMakeLists.txt"; then
  echo "forbidden non-SDK dependency found in submission sources" >&2
  exit 3
fi

rm -rf "$dist_dir"
mkdir -p "$staging_dir"
cp "$project_dir/CMakeLists.txt" "$staging_dir/"
cp -R "$project_dir/src" "$staging_dir/"
cp -R "$project_dir/sdk" "$staging_dir/"
tar -czf "$archive" -C "$dist_dir" CoreGeek
echo "$archive"
