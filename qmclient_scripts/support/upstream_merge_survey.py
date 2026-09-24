#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按区域统计「双方都改过」的文件，为手工融合排序（只读）。

用法：
    py -3 qmclient_scripts/support/upstream_merge_survey.py --path src/game/client
    py -3 qmclient_scripts/support/upstream_merge_survey.py --repo tmp/sync-slice-1 --path src/game/editor --top 15

背景：零重叠可摘的余量已经很薄（见 `docs/development/upstream-sync-log.md` S41：
48 条零重叠里真正独立可摘只有 12 条），剩余差距要靠**按区域手工融合**。
这个脚本给出排序依据：某文件上游有多少提交/改了多少行、本地又改了多少 ——
「上游改动实质 + 本地改动不大」的文件才是性价比最高的融合对象。

口径：
- `上游提交/本地提交` = 窗口内（默认 merge-base..ddnet/master 与 ..HEAD）非 merge 提交数；
- `上游改动/本地改动` = 该区间内该文件的增删行数之和（`git diff --numstat`）；
- 排序按「上游提交 + 本地提交」降序，同级再按本地改动量降序 —— 先看双方都动得多的。

只读：不做任何写仓库的操作。
"""

from __future__ import annotations

import argparse
import subprocess


def git(repo: str, *args: str) -> list[str]:
    proc = subprocess.run(
        ["git", "-C", repo, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        return []
    return [line for line in proc.stdout.splitlines() if line.strip()]


def numstat(repo: str, rev_range: str, path: str) -> tuple[int, int]:
    lines = git(repo, "diff", "--numstat", rev_range, "--", path)
    if not lines:
        return (0, 0)
    parts = lines[0].split("\t")
    try:
        return (int(parts[0]), int(parts[1]))
    except (ValueError, IndexError):
        return (0, 0)


def count_commits(repo: str, rev_range: str, path: str) -> int:
    lines = git(repo, "rev-list", "--no-merges", "--count", rev_range, "--", path)
    if not lines:
        return 0
    try:
        return int(lines[0])
    except ValueError:
        return 0


def sort_rows(rows: list[tuple[str, int, int, int, int]]) -> list[tuple[str, int, int, int, int]]:
    """排序：双方提交数之和降序，其次本地改动量降序（纯函数，便于测试）。"""
    return sorted(rows, key=lambda row: (row[1] + row[2], row[4]), reverse=True)


def format_table(rows: list[tuple[str, int, int, int, int]]) -> list[str]:
    header = f"{'文件':<52}{'上游提交':>8}{'本地提交':>8}{'上游改动':>8}{'本地改动':>8}"
    out = [header]
    for path, up_commits, lo_commits, up_churn, lo_churn in rows:
        out.append(f"{path:<52}{up_commits:>8}{lo_commits:>8}{up_churn:>8}{lo_churn:>8}")
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="按区域统计双方改动，为手工融合排序（只读）")
    parser.add_argument("--repo", default=".", help="仓库/工作树路径")
    parser.add_argument("--base", default=None, help="窗口起点，默认 merge-base HEAD ddnet/master")
    parser.add_argument("--upstream", default="ddnet/master")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--path", dest="pathspec", default="src/game/client")
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    base = args.base
    if not base:
        merged = git(args.repo, "merge-base", args.head, args.upstream)
        if not merged:
            print("错误：无法确定 merge-base，请显式传 --base")
            return 1
        base = merged[0]

    up_range = f"{base}..{args.upstream}"
    lo_range = f"{base}..{args.head}"

    up_files = set(git(args.repo, "diff", "--name-only", up_range, "--", args.pathspec))
    lo_files = set(git(args.repo, "diff", "--name-only", lo_range, "--", args.pathspec))
    both = sorted(up_files & lo_files)

    print(f"窗口: {base[:9]} .. 上游 {args.upstream} / 本地 {args.head}")
    print(f"区域: {args.pathspec}")
    print(f"上游改动文件: {len(up_files)}")
    print(f"本地改动文件: {len(lo_files)}")
    print(f"双方都改过  : {len(both)}")
    print()

    rows = []
    for path in both:
        up_add, up_del = numstat(args.repo, up_range, path)
        lo_add, lo_del = numstat(args.repo, lo_range, path)
        rows.append(
            (
                path,
                count_commits(args.repo, up_range, path),
                count_commits(args.repo, lo_range, path),
                up_add + up_del,
                lo_add + lo_del,
            )
        )

    for line in format_table(sort_rows(rows)[: args.top]):
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
