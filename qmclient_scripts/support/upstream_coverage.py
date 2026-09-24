#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""量化「与上游的内容差距」：对窗口内每条上游非 merge 提交，比较两个引用（起点 / 链尾）的逐行覆盖率。

只读。用法：
    py -3 tmp/coverage_report.py --repo tmp/sync-slice-1 --start 78ffa71b7a --tip sync/slice-15-sweep-c

输出：
- 每个引用的整体逐行覆盖率（下界指标，改写式移植会低估）
- 判定分布（ALREADY-PRESENT / PARTIAL / ABSENT / NO-CODE-CHANGES）
- 两个引用之间的「从 ABSENT/PARTIAL 变为 ALREADY-PRESENT」的提交数（= 本轮实际追上的条数）
- 按顶层目录分组的覆盖率

**口径提醒（两种已知的低估）**
1. 改写式移植：行为等价但代码不同 → 计为未命中；
2. **上游自己搬迁过的内容**：若某提交把数据写进 A 文件、上游后来又把 A 的内容移到 B 文件，
   那么该提交的新增行在当前任何树上都不存在 → 永远计为未命中（实例：Unicode 数据由
   `b0511975af` 写入 `confusables_data.h`，随后 `d64b8fca89` 移到 `.cpp`，即使本地内容与上游逐字节一致，
   这个提交仍显示 0/4394）。因此本指标只适合横向比较与定位热点，不能当作「完成度」。
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = Path(__file__).resolve().parent / "upstream_commit_check.py"
SPEC = importlib.util.spec_from_file_location("upstream_commit_check", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def git(repo: Path, *args: str) -> str:
    proc = subprocess.run(
        ["git", "-C", str(repo), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip())
    return proc.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--start", required=True, help="起点引用（同步前）")
    parser.add_argument("--tip", required=True, help="链尾引用（同步后）")
    parser.add_argument("--base", default=None, help="窗口起点，默认 merge-base HEAD ddnet/master")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    base = args.base or git(repo, "merge-base", "HEAD", "ddnet/master").strip()
    commits = [c for c in git(repo, "rev-list", "--no-merges", f"{base}..ddnet/master").splitlines() if c]

    readers = {ref: CHECK.make_reader(repo, ref) for ref in (args.start, args.tip)}
    stats = {ref: {"present": 0, "total": 0, "verdicts": Counter(), "by_area": defaultdict(lambda: [0, 0])} for ref in readers}
    verdicts_per_commit = {ref: {} for ref in readers}
    diff_cache: dict[str, dict[str, list[str]]] = {}

    for index, commit in enumerate(commits, start=1):
        try:
            diff = git(repo, "show", commit, "--format=", "--unified=0")
        except RuntimeError:
            continue
        additions = CHECK.nontrivial_added_lines(diff)
        diff_cache[commit] = additions
        if not additions:
            continue
        for ref in readers:
            per_file, present, total = CHECK.coverage(additions, readers[ref])
            result = CHECK.verdict(present, total)
            stats[ref]["present"] += present
            stats[ref]["total"] += total
            stats[ref]["verdicts"][result] += 1
            verdicts_per_commit[ref][commit] = result
            for path, (hit, count) in per_file.items():
                area = "/".join(path.split("/")[:3])
                stats[ref]["by_area"][area][0] += hit
                stats[ref]["by_area"][area][1] += count
        if index % 250 == 0:
            print(f"  ...已处理 {index}/{len(commits)}", flush=True)

    for ref in (args.start, args.tip):
        s = stats[ref]
        ratio = (s["present"] / s["total"] * 100.0) if s["total"] else 0.0
        print(f"\n=== {ref} ===")
        print(f"  逐行命中 {s['present']}/{s['total']} = {ratio:.1f}%（下界；改写式移植会低估）")
        for verdict, count in sorted(s["verdicts"].items(), key=lambda kv: -kv[1]):
            print(f"    {verdict:<16} {count}")
        top = sorted(s["by_area"].items(), key=lambda kv: kv[1][1] - kv[1][0], reverse=True)[:8]
        print("  缺口最大的区域（未命中/总数）：")
        for area, (hit, total) in top:
            print(f"    {area:<44} {total - hit}/{total}")

    gained = [
        commit for commit, verdict in verdicts_per_commit[args.tip].items()
        if verdict == "ALREADY-PRESENT" and verdicts_per_commit[args.start].get(commit) != "ALREADY-PRESENT"
    ]
    print(f"\n=== 本轮实际追上（{args.start} 非 ALREADY-PRESENT → {args.tip} ALREADY-PRESENT）：{len(gained)} 条 ===")
    for commit in gained[:40]:
        print(f"    {commit[:10]} {git(repo, 'log', '-1', '--format=%s', commit).strip()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
