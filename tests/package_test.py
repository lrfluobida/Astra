#!/usr/bin/env python3
import subprocess
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main():
    result = subprocess.run(
        ["bash", "scripts/package_sdk.sh"],
        cwd=str(ROOT),
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    archive = Path(result.stdout.strip())
    if not archive.is_file():
        raise AssertionError("submission archive was not created")
    with tarfile.open(archive, "r:gz") as package:
        names = set(package.getnames())
        required = {
            "SDK/CoreGeek/CMakeLists.txt",
            "SDK/CoreGeek/src/main.cpp",
            "SDK/CoreGeek/src/json_io.cpp",
            "SDK/CoreGeek/src/http_server.cpp",
        }
        if not required.issubset(names):
            raise AssertionError("submission archive is missing required sources")
        forbidden = ("third_party", "nlohmann", "httplib", "/tests/", "/build/", "/.tmp/")
        if any(any(part in name for part in forbidden) for name in names):
            raise AssertionError("submission archive contains forbidden development files")
    print("PASS official SDK submission package")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
