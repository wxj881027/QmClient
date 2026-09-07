#!/usr/bin/env python3
"""Run the instrumented unit suite and collect scoped GCC line/branch coverage."""

import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
SCOPES = ("src/game/client/components/qmclient/", "src/game/client/ui/")


def merge_document(document, object_id, files, root=ROOT):
    working = Path(document.get("current_working_directory", str(root)))
    for source in document.get("files", []):
        path = Path(source["file"])
        path = (path if path.is_absolute() else working / path).resolve()
        if not path.is_relative_to(root):
            continue
        name = path.relative_to(root).as_posix()
        if not name.startswith(SCOPES):
            continue
        entry = files.setdefault(name, {"lines": {}, "branches": {}})
        for line in source.get("lines", []):
            number = int(line["line_number"])
            hit = int(line["count"]) > 0
            entry["lines"][number] = entry["lines"].get(number, False) or hit
            for index, branch in enumerate(line.get("branches", [])):
                # 分支按编译实例保留，不把不同模板实例或 translation unit 合并成一个分支。
                key = (object_id, line.get("function_name", ""), number, index)
                entry["branches"][key] = entry["branches"].get(key, False) or int(branch["count"]) > 0


def summarize(files):
    results = {}
    for name, entry in sorted(files.items()):
        results[name] = {
            "lines": len(entry["lines"]),
            "lines_covered": sum(entry["lines"].values()),
            "branches": len(entry["branches"]),
            "branches_covered": sum(entry["branches"].values()),
            "uncovered_lines": sorted(number for number, hit in entry["lines"].items() if not hit),
        }
    totals = {
        key: sum(entry[key] for entry in results.values())
        for key in ("lines", "lines_covered", "branches", "branches_covered")
    }
    for kind in ("lines", "branches"):
        totals[f"{kind}_percent"] = (
            round(100 * totals[f"{kind}_covered"] / totals[kind], 2) if totals[kind] else None
        )
    return results, totals


def run(build, output, gcov, test_work_root=None):
    build, output = build.resolve(), output.resolve()
    if build.parent != ROOT or build.name != "cmake-build-linux":
        raise ValueError("use the repository's cmake-build-linux directory")
    if not output.is_relative_to(ROOT / "tmp"):
        raise ValueError("coverage output must be below repository tmp/")
    executable = build / "testrunner"
    objects = sorted(build.rglob("*.gcno"))
    if not executable.is_file() or not objects:
        raise ValueError("instrumented testrunner / .gcno files missing; configure GCC with --coverage first")
    output.mkdir(parents=True, exist_ok=True)
    test_work_root = test_work_root.resolve() if test_work_root else output
    if not test_work_root.is_dir():
        raise ValueError(f"test work root must be an existing directory: {test_work_root}")
    gcov_path = shutil.which(gcov)
    if not gcov_path:
        raise ValueError(f"gcov not found: {gcov}")
    gcov_version = subprocess.check_output([gcov_path, "--version"], text=True).splitlines()[0]
    # 只重置已确认构建目录内的 GCC 计数器；不删除构建产物或用户数据。
    for counter in build.rglob("*.gcda"):
        if not counter.resolve().is_relative_to(build):
            raise ValueError(f"counter outside build directory: {counter}")
        counter.unlink()

    test_result = output / "tests.xml"
    test_result.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="qm-test-work-", dir=test_work_root) as work:
        with (output / "tests.log").open("w", encoding="utf-8") as log:
            tests = subprocess.run(
                [str(executable), f"--gtest_output=xml:{test_result}", "--gtest_brief=1"],
                cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=600, check=False,
            )
    files = {}
    errors = []
    with tempfile.TemporaryDirectory(prefix="gcov-work-", dir=output) as work:
        work = Path(work)
        with (output / "gcov.log").open("w", encoding="utf-8") as log:
            for obj in objects:
                object_id = obj.relative_to(build).as_posix()
                result = subprocess.run(
                    [gcov_path, "--json-format", "--branch-probabilities", "--branch-counts", str(obj)],
                    cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=60, check=False,
                )
                if result.returncode:
                    errors.append({"object": object_id, "exit_code": result.returncode})
                for report in work.glob("*.gcov.json.gz"):
                    with gzip.open(report, "rt", encoding="utf-8") as stream:
                        document = json.load(stream)
                    merge_document(document, object_id, files)
                    report.unlink()
    per_file, totals = summarize(files)
    uninstrumented = sorted(
        path.relative_to(ROOT).as_posix()
        for scope in SCOPES
        for path in (ROOT / scope).rglob("*")
        if path.suffix in (".cpp", ".h") and path.relative_to(ROOT).as_posix() not in per_file
    )
    disabled = []
    if test_result.is_file():
        for case in ET.parse(test_result).iter("testcase"):
            if case.get("status") == "notrun":
                disabled.append(f'{case.get("classname")}.{case.get("name")}')
    else:
        errors.append({"error": "GoogleTest XML result missing"})
    report = {
        "schema": 1,
        "scope": list(SCOPES),
        "source_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
        ).strip(),
        "worktree_dirty": bool(subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True,
        ).strip()),
        "gcov": gcov_version,
        "test_work_root": str(test_work_root),
        "measurement": "unit-tests-only; source lines unioned, branches counted per compilation instance",
        "uninstrumented_sources": uninstrumented,
        "test_exit_code": tests.returncode,
        "disabled_tests": disabled,
        "collection_errors": errors,
        "totals": totals,
        "files": per_file,
    }
    (output / "coverage.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"totals": totals, "test_exit_code": tests.returncode, "collection_errors": len(errors)}, indent=2))
    return 0 if tests.returncode == 0 and not errors and totals["lines_covered"] > 0 else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=ROOT / "cmake-build-linux")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tmp/qm-coverage")
    parser.add_argument("--gcov", default="gcov")
    parser.add_argument(
        "--test-work-root", type=Path,
        help="existing scratch directory for test files, e.g. /tmp on WSL; reports remain in --output-dir",
    )
    args = parser.parse_args()
    try:
        return run(args.build_dir, args.output_dir, args.gcov, args.test_work_root)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"coverage collection failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
