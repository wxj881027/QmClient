# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""Classify changed paths for GitHub Actions jobs.

The workflow itself should still start so required checks can finish. Heavy
jobs can then skip themselves with a successful "skipped" conclusion when the
diff only touches files that cannot affect the client build or analysis.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]

BUILD_PATH_PREFIXES = (
    "cmake/",
    "ddnet-libs/",
    "other/",
    "src/",
)
BUILD_EXACT_PATHS = (
    "CMakeLists.txt",
    "Cargo.lock",
    "Cargo.toml",
    "Dockerfile",
    "deny.toml",
    "memcheck.supp",
    "valgrind.supp",
)
BUILD_FILE_RE = re.compile(r".*\.(c|cc|cpp|cxx|h|hh|hpp|hxx|rs|cmake)$")
RUNTIME_PATH_PREFIXES = ("data/", "datasrc/")
CI_WORKFLOW_PREFIX = ".github/workflows/"
PYTHON_FILE_RE = re.compile(r".*\.py$")


def git(*args: str) -> str:
    proc = subprocess.run(
        ["git", "-c", "core.safecrlf=false", *args],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return proc.stdout


def unique_paths(text: str) -> list[str]:
    seen: set[str] = set()
    paths: list[str] = []
    for line in text.splitlines():
        path = line.strip().replace("\\", "/")
        if path and path not in seen:
            seen.add(path)
            paths.append(path)
    return paths


def event_payload() -> dict:
    event_path = os.environ.get("GITHUB_EVENT_PATH")
    if not event_path:
        return {}
    try:
        return json.loads(Path(event_path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def changed_paths() -> tuple[list[str], str]:
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    payload = event_payload()

    if event_name == "pull_request":
        base_ref = payload.get("pull_request", {}).get("base", {}).get("ref")
        if base_ref:
            git("fetch", "--no-tags", "--depth=1", "origin", base_ref)
            return unique_paths(
                git("diff", "--name-only", f"origin/{base_ref}...HEAD")
            ), (f"origin/{base_ref}...HEAD")

    if event_name == "push":
        before = payload.get("before")
        after = payload.get("after") or os.environ.get("GITHUB_SHA", "HEAD")
        if before and not re.fullmatch(r"0{40}", before):
            return unique_paths(git("diff", "--name-only", f"{before}..{after}")), (
                f"{before}..{after}"
            )

    # Merge queue: 用 payload 里的 base_sha..head_sha 做精确 diff，
    # 避免每次 merge_group 都触发全部重量级 CI。
    if event_name == "merge_group":
        mg = payload.get("merge_group", {})
        base_sha = mg.get("base_sha")
        head_sha = mg.get("head_sha") or os.environ.get("GITHUB_SHA", "HEAD")
        if base_sha and head_sha:
            return unique_paths(
                git("diff", "--name-only", f"{base_sha}..{head_sha}")
            ), (f"{base_sha}..{head_sha}")
        # payload 缺字段时回退到 HEAD^..HEAD
        return unique_paths(git("diff", "--name-only", "HEAD^..HEAD")), "HEAD^..HEAD"

    return unique_paths(git("diff", "--name-only", "HEAD^..HEAD")), "HEAD^..HEAD"


def is_heavy_path(path: str) -> bool:
    if path in ("<merge_group>", "<unknown>"):
        return True
    if path.startswith(CI_WORKFLOW_PREFIX):
        return True
    if path in BUILD_EXACT_PATHS:
        return True
    if path.startswith(BUILD_PATH_PREFIXES):
        return True
    if BUILD_FILE_RE.fullmatch(path):
        return True
    return False


def is_runtime_path(path: str) -> bool:
    if path == "<merge_group>":
        return True
    if is_heavy_path(path):
        return True
    return path.startswith(RUNTIME_PATH_PREFIXES)


def is_python_path(path: str) -> bool:
    if path in ("<merge_group>", "<unknown>"):
        return True
    return bool(PYTHON_FILE_RE.fullmatch(path))


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def write_github_output(values: dict[str, str]) -> None:
    output = os.environ.get("GITHUB_OUTPUT")
    if not output:
        return
    with open(output, "a", encoding="utf-8", newline="\n") as f:
        for key, value in values.items():
            f.write(f"{key}={value}\n")


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()

    try:
        paths, base = changed_paths()
    except Exception as exc:
        print(f"Failed to classify changed paths: {exc}", file=sys.stderr)
        paths = ["<unknown>"]
        base = "fallback"

    heavy_changed = any(is_heavy_path(path) for path in paths)
    runtime_changed = any(is_runtime_path(path) for path in paths)
    python_changed = any(is_python_path(path) for path in paths)
    values = {
        "base": base,
        "changed_count": str(len(paths)),
        "heavy_changed": bool_text(heavy_changed),
        "runtime_changed": bool_text(runtime_changed),
        "python_changed": bool_text(python_changed),
    }
    write_github_output(values)

    print(f"Base: {base}")
    print(f"Changed files: {len(paths)}")
    print(f"heavy_changed={values['heavy_changed']}")
    print(f"runtime_changed={values['runtime_changed']}")
    print(f"python_changed={values['python_changed']}")
    for path in paths[:80]:
        print(f"- {path}")
    if len(paths) > 80:
        print(f"- ... and {len(paths) - 80} more")
    return 0


def self_test() -> int:
    cases = [
        ("src/game/client/gameclient.cpp", True, False),
        ("src/game/client/gameclient.h", True, False),
        ("Cargo.toml", True, False),
        ("cmake/FindFoo.cmake", True, False),
        ("docs/example.md", False, False),
        ("README.md", False, False),
        ("data/editor/entities.png", False, True, False),
        ("qmclient_scripts/gate/check_gate.py", False, True),
        (".github/workflows/style.yml", True, False),
    ]
    failed = False
    for case in cases:
        if len(case) == 3:
            path, expected_heavy, expected_python = case
            expected_runtime = expected_heavy
        else:
            path, expected_heavy, expected_runtime, expected_python = case
        heavy = is_heavy_path(path)
        runtime = is_runtime_path(path)
        python = is_python_path(path)
        if (
            heavy != expected_heavy
            or runtime != expected_runtime
            or python != expected_python
        ):
            print(
                f"FAIL {path}: heavy={heavy} runtime={runtime} python={python}, "
                f"expected heavy={expected_heavy} runtime={expected_runtime} "
                f"python={expected_python}"
            )
            failed = True
    if failed:
        return 1
    print("ci_changed_paths self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
