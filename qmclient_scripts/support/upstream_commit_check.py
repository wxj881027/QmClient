#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""判断某个上游提交在本地是否**已经存在**（内容级），避免「cherry-pick 无冲突但重复插入」的事故。

用法：
    py -3 qmclient_scripts/support/upstream_commit_check.py <commit> [<commit> ...]
    py -3 qmclient_scripts/support/upstream_commit_check.py --range <A>..<B>
    py -3 qmclient_scripts/support/upstream_commit_check.py --repo <worktree 路径> <commit>

为什么需要它：QmClient 历史上做过「改写式摘取」（把上游改动手工搬进本地结构），
因此 `git cherry-pick` 可能**无冲突地成功**，却把同一段代码插第二遍（例如枚举/常量重定义）。
本脚本按「上游提交新增行在本地当前工作树中的命中率」给出判定：

    ALREADY-PRESENT  命中率 ≥ 0.9 —— 内容已在本地，不要 cherry-pick
    PARTIAL          0.15 ≤ 命中率 < 0.9 —— 部分已在，必须手工融合后编译验证
    ABSENT           命中率 < 0.15 —— 大概率可直接 cherry-pick（仍需编译验证）
    NO-CODE-CHANGES  该提交没有可比较的代码新增行（纯文档/注释等）

只读：不修改任何文件、不切换分支。判定口径与流程见 `docs/development/upstream-sync-plan.md`。
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ALREADY_PRESENT_THRESHOLD = 0.9
PARTIAL_THRESHOLD = 0.15
MIN_LINE_LENGTH = 6
SKIP_LINES = {
    "{",
    "}",
    "};",
    ");",
    "};",
    "else",
    "#endif",
    "#else",
    "break;",
    "continue;",
    "return;",
}


def git(repo: Path, *args: str) -> str:
    proc = subprocess.run(
        ["git", "-C", str(repo), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} 失败：{proc.stderr.strip()}")
    return proc.stdout


def nontrivial_added_lines(diff_text: str) -> dict[str, list[str]]:
    """从 `git show --unified=0` 输出里提取「非平凡」新增行（按文件分组，去缩进便于跨格式比对）。"""
    additions: dict[str, list[str]] = {}
    current: str | None = None
    for line in diff_text.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:]
            additions.setdefault(current, [])
            continue
        if not line.startswith("+") or line.startswith("+++"):
            continue
        if current is None:
            continue
        stripped = line[1:].strip()
        if len(stripped) < MIN_LINE_LENGTH or stripped in SKIP_LINES:
            continue
        if stripped.startswith(("//", "/*", "*", "#include <", "#include \"")):
            continue
        additions[current].append(stripped)
    return {path: lines for path, lines in additions.items() if lines}


def coverage(
    additions: dict[str, list[str]], reader
) -> tuple[dict[str, tuple[int, int]], int, int]:
    """返回 (每文件命中/总数, 总命中, 总行数)。reader(path) 返回文件文本或 None。"""
    per_file: dict[str, tuple[int, int]] = {}
    present = 0
    total = 0
    for path, lines in additions.items():
        text = reader(path)
        pool = {line.strip() for line in text.splitlines()} if text is not None else set()
        hit = sum(1 for line in lines if line in pool)
        per_file[path] = (hit, len(lines))
        present += hit
        total += len(lines)
    return per_file, present, total


def verdict(present: int, total: int) -> str:
    if total == 0:
        return "NO-CODE-CHANGES"
    ratio = present / total
    if ratio >= ALREADY_PRESENT_THRESHOLD:
        return "ALREADY-PRESENT"
    if ratio >= PARTIAL_THRESHOLD:
        return "PARTIAL"
    return "ABSENT"


ADVICE = {
    "ALREADY-PRESENT": "内容已在本地 —— 不要 cherry-pick（会被重复插入，例如枚举/常量重定义）",
    "PARTIAL": "部分已在 —— 需要手工融合，融合后必须编译",
    "ABSENT": "大概率可直接 cherry-pick —— 仍需编译验证",
    "NO-CODE-CHANGES": "没有可比较的代码新增行（纯文档/注释/删除类），按人工判断处理",
}


def make_reader(repo: Path, ref: str | None):
    """返回 reader(path) -> str|None。ref 为空时读工作树，否则读该 git 引用的内容。"""
    cache: dict[tuple[str | None, str], str | None] = {}

    def reader(path: str) -> str | None:
        key = (ref, path)
        if key in cache:
            return cache[key]
        if ref is None:
            target = repo / path
            text = target.read_text(encoding="utf-8", errors="replace") if target.is_file() else None
        else:
            proc = subprocess.run(
                ["git", "-C", str(repo), "show", f"{ref}:{path}"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            text = proc.stdout if proc.returncode == 0 else None
        cache[key] = text
        return text

    return reader


_REVERT_CACHE: dict[str, dict[str, str]] = {}


def revert_map(repo: Path) -> dict[str, str]:
    """返回 {被回退的提交标题: 回退提交短哈希}（启发式，供人工判断用）。"""
    key = str(repo)
    if key in _REVERT_CACHE:
        return _REVERT_CACHE[key]
    result: dict[str, str] = {}
    proc = subprocess.run(
        ["git", "-C", str(repo), "log", "--no-merges", "--all", "--format=%h\t%s"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    for line in proc.stdout.splitlines():
        if "\t" not in line:
            continue
        short, subject = line.split("\t", 1)
        if subject.startswith('Revert "') and subject.endswith('"'):
            result.setdefault(subject[len('Revert "') : -1], short)
    _REVERT_CACHE[key] = result
    return result


def revert_warning(repo: Path, subject: str) -> str | None:
    reverts = revert_map(repo)
    if subject not in reverts:
        return None
    revert_hash = reverts[subject]
    reapply = reverts.get(f'Revert "{subject}"')
    if reapply:
        return f"曾被回退（{revert_hash}），但该回退又被回退（{reapply}）—— 净效果是保留"
    return f"**已被上游回退**（{revert_hash}）—— 不要摘，或应改为摘那条回退"


def check_commit(repo: Path, commit: str, quiet: bool = False, ref: str | None = None) -> str:
    header = git(repo, "show", "-s", "--format=%h %ad %s", "--date=short", commit).strip()
    subject = git(repo, "show", "-s", "--format=%s", commit).strip()
    diff = git(repo, "show", commit, "--format=", "--unified=0")
    additions = nontrivial_added_lines(diff)

    per_file, present, total = coverage(additions, make_reader(repo, ref))
    result = verdict(present, total)
    warning = revert_warning(repo, subject)

    if not quiet:
        print(header + (f"  [ref={ref}]" if ref else ""))
        for path, (hit, count) in sorted(per_file.items()):
            print(f"    {path}: {hit}/{count}")
        ratio = (present / total * 100.0) if total else 0.0
        print(f"    新增行命中 {present}/{total} ({ratio:.1f}%) -> {result}")
        print(f"    建议：{ADVICE[result]}")
        if warning:
            print(f"    ⚠ 回退检查：{warning}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="判断上游提交在本地是否已存在（只读）")
    parser.add_argument("commits", nargs="*", help="上游提交（可多个）")
    parser.add_argument("--range", dest="commit_range", help="提交范围，例如 <A>..<B>")
    parser.add_argument("--repo", help="仓库/工作树路径，默认当前目录")
    parser.add_argument("--quiet", action="store_true", help="只输出判定结果")
    parser.add_argument("--ref", help="不读工作树，改读该 git 引用（如某个提交或分支）的内容")
    args = parser.parse_args()

    repo = Path(args.repo).resolve() if args.repo else Path(git(Path.cwd(), "rev-parse", "--show-toplevel").strip())

    commits = list(args.commits)
    if args.commit_range:
        commits.extend(
            line for line in git(repo, "rev-list", "--no-merges", args.commit_range).splitlines() if line
        )
    if not commits:
        parser.error("至少给一个提交，或用 --range")

    for commit in commits:
        try:
            result = check_commit(repo, commit, quiet=args.quiet, ref=args.ref)
        except RuntimeError as exc:
            print(f"错误：{exc}", file=sys.stderr)
            return 1
        if args.quiet:
            print(f"{result}\t{commit}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
