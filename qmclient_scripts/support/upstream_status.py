#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DDNet 上游跟进状态快照（只读）。

用法：
    py -3 qmclient_scripts/support/upstream_status.py
    py -3 qmclient_scripts/support/upstream_status.py --clean-list
    py -3 qmclient_scripts/support/upstream_status.py --conflicts
    py -3 qmclient_scripts/support/upstream_status.py --json tmp/upstream-status.json

说明：
- 每次同步切片前先跑一次，拿到 merge-base、领先/落后提交数、双方都改过的文件数；
- 「零重叠提交」= 只改动了 fork 自 merge-base 以来从未碰过的文件，通常可直接
  cherry-pick 而无需解冲突，`--clean-list` 打印这些提交（仍需注意提交间依赖）；
- `--conflicts` 用 `git merge-tree` 干跑一次合并，只列冲突路径，不建分支、不改工作树；
- 本脚本不写任何仓库文件（`--json` 除外），不会切换分支或修改工作树。

计划与判定记录见 `docs/development/upstream-sync-plan.md`。
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_UPSTREAM = "ddnet/master"
DEFAULT_ORIGIN_REF = "origin/master"
CHUNK_SIZE = 200
PATH_RE = re.compile(r"^[A-Za-z0-9_.@+\-/ ]+$")
HEX_RE = re.compile(r"^[0-9a-f]{40,64}$")


def git(*args: str) -> str:
    """执行 git 并返回 stdout；失败时抛出 RuntimeError。"""
    proc = subprocess.run(
        ["git", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} 失败：{proc.stderr.strip()}")
    return proc.stdout


def git_lines(*args: str) -> list[str]:
    return [line for line in git(*args).splitlines() if line.strip()]


def repo_root() -> Path:
    return Path(git("rev-parse", "--show-toplevel").strip())


def area_of(path: str, depth: int = 3) -> str:
    parts = path.split("/")
    return "/".join(parts[: min(depth, len(parts))])


def merge_base(upstream: str) -> str:
    return git("merge-base", "HEAD", upstream).strip()


def collect_baseline_freshness(origin_ref: str = DEFAULT_ORIGIN_REF) -> dict:
    """本地基线相对 origin/master 是否落后。

    教训来源：upstream-sync-log.md S37 —— 维护者同一时间修掉了本地记为「既有失败」的问题，
    动手前先 fetch + 看 origin/master 能省掉一整轮重复劳动。
    """
    probe = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", origin_ref],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if probe.returncode != 0:
        return {"origin_ref": origin_ref, "exists": False, "behind": 0, "tip": ""}
    behind = int(git("rev-list", "--count", f"HEAD..{origin_ref}").strip() or "0")
    tip = git("log", "-1", "--format=%h %ad %s", "--date=short", origin_ref).strip()
    return {"origin_ref": origin_ref, "exists": True, "behind": behind, "tip": tip}


def format_baseline_freshness(info: dict) -> str:
    """把基线新鲜度格式化成一行；落后时给出动作提示。"""
    label = "基线新鲜度        :"
    origin_ref = info.get("origin_ref", DEFAULT_ORIGIN_REF)
    if not info.get("exists"):
        return f"{label} 未找到 {origin_ref}（先 git fetch origin）"
    if info["behind"] <= 0:
        return f"{label} 已跟上 {origin_ref}（{info['tip']}）"
    return (
        f"{label} ⚠ 落后 {origin_ref} {info['behind']} 个提交（{info['tip']}）"
        " —— 动手前先前移基线，避免重复劳动（见 upstream-sync-log.md S37）"
    )


def collect_status(upstream: str) -> dict:
    base = merge_base(upstream)
    left, right = git("rev-list", "--left-right", "--count", f"HEAD...{upstream}").split()
    local_changed = set(git_lines("diff", "--name-only", base, "HEAD"))
    upstream_changed = set(git_lines("diff", "--name-only", base, upstream))
    overlap = sorted(local_changed & upstream_changed)

    all_commits = git_lines("rev-list", "--no-merges", f"{base}..{upstream}")
    risky: set[str] = set()
    for index in range(0, len(overlap), CHUNK_SIZE):
        chunk = overlap[index : index + CHUNK_SIZE]
        risky.update(
            git_lines("log", "--no-merges", "--format=%H", f"{base}..{upstream}", "--", *chunk)
        )
    clean = [commit for commit in all_commits if commit not in risky]
    prereq = {commit: missing_files_for_commit(commit) for commit in clean}
    clean_pickable = [commit for commit in clean if not prereq[commit]]
    clean_with_prereq = [commit for commit in clean if prereq[commit]]

    return {
        "upstream": upstream,
        "merge_base": base,
        "merge_base_subject": git("log", "-1", "--format=%h %ad %s", "--date=short", base).strip(),
        "ahead": int(left),
        "behind": int(right),
        "local_changed_files": len(local_changed),
        "upstream_changed_files": len(upstream_changed),
        "overlap_files": overlap,
        "upstream_commits": len(all_commits),
        "upstream_commits_touching_overlap": len(risky),
        "clean_commits": clean,
        "clean_commits_pickable": clean_pickable,
        "clean_commits_with_prereq": clean_with_prereq,
        "clean_commit_prereq_files": prereq,
    }


def missing_files_for_commit(commit: str) -> list[str]:
    """返回「该提交会改动、但本地 HEAD 里不存在」的文件。

    这类提交不是独立可摘的：它们改的是同步点之后上游**新建**的文件（例如上游把
    `menus_settings.cpp` 拆成 `menus_settings_ddnet.cpp`、新增 `font_icons.h` 等），
    cherry-pick 会直接 `DU`（本地已删/不存在、上游修改）失败 —— 见 upstream-sync-log.md S41。
    提交自己**新增**（A）的文件不算缺前置。
    """
    missing: list[str] = []
    for line in git_lines("diff-tree", "--no-commit-id", "--name-status", "-r", commit):
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        status = parts[0][:1]
        path = parts[-1]
        if status == "A":
            continue
        probe = subprocess.run(
            ["git", "cat-file", "-e", f"HEAD:{path}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if probe.returncode != 0:
            missing.append(path)
    return missing


def collect_conflicts(upstream: str) -> dict:
    proc = subprocess.run(
        ["git", "merge-tree", "--write-tree", "--name-only", "HEAD", upstream],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if not proc.stdout.strip():
        raise RuntimeError(f"git merge-tree 无输出：{proc.stderr.strip()}")

    paths: list[str] = []
    types: dict[str, int] = {}
    for line in proc.stdout.splitlines():
        stripped = line.strip()
        if not stripped or HEX_RE.match(stripped):
            continue
        if stripped.startswith("CONFLICT"):
            match = re.match(r"CONFLICT \(([^)]+)\)", stripped)
            key = match.group(1) if match else "unknown"
            types[key] = types.get(key, 0) + 1
            continue
        if stripped.startswith(("Auto-merging", "Failed to merge submodule")):
            continue
        if PATH_RE.match(stripped):
            paths.append(stripped)

    by_area: dict[str, int] = {}
    for path in paths:
        key = area_of(path)
        by_area[key] = by_area.get(key, 0) + 1
    return {
        "conflict_paths": sorted(paths),
        "conflict_types": types,
        "conflict_by_area": dict(sorted(by_area.items(), key=lambda item: -item[1])),
    }


def print_summary(status: dict) -> None:
    print(f"上游引用          : {status['upstream']}")
    print(f"上次同步点        : {status['merge_base_subject']}")
    print(f"领先 / 落后       : {status['ahead']} / {status['behind']}")
    print(f"本地改动文件      : {status['local_changed_files']}")
    print(f"上游改动文件      : {status['upstream_changed_files']}")
    print(f"双方都改过        : {len(status['overlap_files'])}")
    print(f"上游非合并提交    : {status['upstream_commits']}")
    print(f"  其中与 fork 重叠: {status['upstream_commits_touching_overlap']}")
    print(f"  零重叠(可直接摘): {len(status['clean_commits'])}")
    print(f"    其中独立可摘  : {len(status['clean_commits_pickable'])}")
    print(
        f"    需前置(改上游新文件): {len(status['clean_commits_with_prereq'])}"
        " —— 这些提交改的文件本地不存在，cherry-pick 会 DU 失败"
    )


def format_clean_entry(commit: str, subject: str, missing_files: list[str]) -> str:
    """零重叠清单的一行；缺前置时标出来并列出文件。"""
    head = f"  {commit[:9]}  {subject}"
    if not missing_files:
        return head
    return head + "\n      ⚠ 需前置（本地不存在）: " + "、".join(missing_files[:3]) + (
        " 等" if len(missing_files) > 3 else ""
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="DDNet 上游跟进状态快照（只读）")
    parser.add_argument("--upstream", default=DEFAULT_UPSTREAM, help=f"上游引用，默认 {DEFAULT_UPSTREAM}")
    parser.add_argument("--clean-list", action="store_true", help="打印零重叠提交及其标题")
    parser.add_argument("--conflicts", action="store_true", help="干跑合并并列出冲突路径（较慢）")
    parser.add_argument("--json", metavar="PATH", help="把结果写入 JSON 文件")
    parser.add_argument(
        "--origin-ref",
        default=DEFAULT_ORIGIN_REF,
        help=f"用哪个引用判断本地基线是否落后，默认 {DEFAULT_ORIGIN_REF}",
    )
    parser.add_argument("--skip-freshness", action="store_true", help="不检查基线新鲜度")
    args = parser.parse_args()

    try:
        status = collect_status(args.upstream)
    except RuntimeError as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1

    print_summary(status)

    if not args.skip_freshness:
        try:
            freshness = collect_baseline_freshness(args.origin_ref)
        except RuntimeError as exc:
            print(f"基线新鲜度        : 检查失败（{exc}）")
        else:
            status["baseline_freshness"] = freshness
            print(format_baseline_freshness(freshness))

    if args.clean_list:
        prereq = status.get("clean_commit_prereq_files", {})
        pickable = status.get("clean_commits_pickable", status["clean_commits"])
        print(f"\n零重叠且独立可摘（{len(pickable)} 条）：")
        for commit in pickable:
            subject = git("log", "-1", "--format=%s", commit).strip()
            print(format_clean_entry(commit, subject, []))
        with_prereq = status.get("clean_commits_with_prereq", [])
        if with_prereq:
            print(f"\n零重叠但需前置提交（{len(with_prereq)} 条，现在摘会 DU 失败）：")
            for commit in with_prereq:
                subject = git("log", "-1", "--format=%s", commit).strip()
                print(format_clean_entry(commit, subject, prereq.get(commit, [])))

    if args.conflicts:
        try:
            conflicts = collect_conflicts(args.upstream)
        except RuntimeError as exc:
            print(f"错误：{exc}", file=sys.stderr)
            return 1
        status["conflicts"] = conflicts
        print(f"\n干跑合并冲突路径  : {len(conflicts['conflict_paths'])}")
        print("冲突类型：")
        for key, count in sorted(conflicts["conflict_types"].items(), key=lambda item: -item[1]):
            print(f"  {key:<16} {count}")
        print("冲突最集中的目录：")
        for area, count in list(conflicts["conflict_by_area"].items())[:12]:
            print(f"  {area:<44} {count}")

    if args.json:
        path = Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(status, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\n已写入 {path.as_posix()}（仓库根 {repo_root().as_posix()}）")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
