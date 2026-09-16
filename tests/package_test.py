#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from http_test import run_server


ROOT = Path(__file__).resolve().parents[1]


def smoke_test_package(package):
    cmake = os.environ.get("CMAKE") or shutil.which("cmake")
    if not cmake:
        print("SKIP standalone package build: cmake was not found")
        return

    with tempfile.TemporaryDirectory(prefix="astra-package-") as temporary:
        extract_dir = Path(temporary)
        package.extractall(extract_dir)
        project_dir = extract_dir / "CoreGeek"
        build_dir = project_dir / "build"
        configure = [cmake, "-S", str(project_dir), "-B", str(build_dir)]
        make_program = os.environ.get("CMAKE_MAKE_PROGRAM")
        if make_program:
            configure.append("-DCMAKE_MAKE_PROGRAM={}".format(make_program))
        subprocess.run(configure, check=True)
        subprocess.run([cmake, "--build", str(build_dir), "--parallel", "2"], check=True)

        binary = project_dir / "bin" / "CoreGeek"
        if not binary.is_file():
            raise AssertionError("standalone build did not create bin/CoreGeek")

        with (ROOT / "tests" / "fixtures" / "minimal_turn.json").open(
            "r", encoding="utf-8"
        ) as source:
            fixture = json.load(source)
        library_path = os.environ.pop("LD_LIBRARY_PATH", None)
        try:
            run_server(lambda port: [str(binary), str(port)], fixture)
        finally:
            if library_path is not None:
                os.environ["LD_LIBRARY_PATH"] = library_path


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
            "CoreGeek/CMakeLists.txt",
            "CoreGeek/src/main.cpp",
            "CoreGeek/src/json_io.cpp",
            "CoreGeek/src/http_server.cpp",
            "CoreGeek/sdk/jsoncpp/json/json.h",
            "CoreGeek/sdk/jsoncpp/jsoncpp.cpp",
            "CoreGeek/sdk/jsoncpp/LICENSE",
            "CoreGeek/sdk/jsoncpp/SOURCE.md",
        }
        if not required.issubset(names):
            raise AssertionError("submission archive is missing required sources")
        forbidden = ("third_party", "nlohmann", "httplib", "/tests/", "/build/", "/.tmp/")
        if any(any(part in name for part in forbidden) for name in names):
            raise AssertionError("submission archive contains forbidden development files")
        if any(name.startswith("SDK/") for name in names):
            raise AssertionError("submission archive must start at the CoreGeek directory")
        smoke_test_package(package)
    print("PASS self-contained SDK submission package")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
