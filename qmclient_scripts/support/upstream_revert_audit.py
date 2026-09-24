#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""自审：同步链里 cherry-pick 进来的上游提交，有没有后来被上游回退掉的？

只读。用法：
    py -3 tmp/audit_chain_reverts.py --repo tmp/sync-slice-1 --base aea1453cc7 --tip sync/slice-17-sweep-d

做法：
1. 遍历链上每个提交，解析 `(cherry picked from commit <sha>)` 取回原始上游 sha；
2. 用 upstream_commit_check 的回退表（扫描所有 `Revert "X"` 提交）判断该上游提交是否被回退；
3. 若被回退：再检查回退本身是否又被回退（净效果保留则只提示）。
"""

from __future__ import annotations

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = Path(__file__).resolve().parent / "upstream_commit_check.py"
SPEC = importlib.util.spec_from_file_location("upstream_commit_check", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)

TRAILER = re.compile(r"cherry picked from commit ([0-9a-f]{7,40})")


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
    parser.add_argument("--base", required=True)
    parser.add_argument("--tip", required=True)
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    reverts = CHECK.revert_map(repo)

    raw = git(repo, "log", "--format=%H%x01%B%x02", f"{args.base}..{args.tip}")
    entries = []
    chain_subjects = set()
    for record in raw.split("\x02"):
        record = record.strip("\n")
        if not record:
            continue
        chain_hash, _, body = record.partition("\x01")
        chain_subjects.add(body.strip().splitlines()[0])
        match = TRAILER.search(body)
        if match:
            entries.append((chain_hash.strip(), match.group(1)))

    print(f"链上带 -x 溯源的提交：{len(entries)} 条")
    reverted = []
    reapplied = []
    already_handled = []
    for chain_hash, upstream in entries:
        subject = git(repo, "log", "-1", "--format=%s", upstream).strip()
        if subject not in reverts:
            continue
        revert_hash = reverts[subject]
        reapply_hash = reverts.get(f'Revert "{subject}"')
        if reapply_hash:
            reapplied.append((chain_hash, upstream, subject, revert_hash, reapply_hash))
        elif f'Revert "{subject}"' in chain_subjects:
            already_handled.append((chain_hash, upstream, subject, revert_hash))
        else:
            reverted.append((chain_hash, upstream, subject, revert_hash))

    print(f"\n=== 已被上游回退且链上还没有回退：{len(reverted)} 条（需要处理） ===")
    for chain_hash, upstream, subject, revert_hash in reverted:
        print(f"  链上 {chain_hash[:10]}  ←  上游 {upstream[:10]}  {subject}")
        print(f"      回退提交：{revert_hash}")

    print(f"\n=== 已被上游回退、但链上已包含回退：{len(already_handled)} 条（无需处理） ===")
    for chain_hash, upstream, subject, revert_hash in already_handled:
        print(f"  链上 {chain_hash[:10]}  ←  上游 {upstream[:10]}  {subject}")
        print(f"      回退提交：{revert_hash}")

    print(f"\n=== 曾回退但又被回退（净效果保留）：{len(reapplied)} 条（无需处理） ===")
    for chain_hash, upstream, subject, revert_hash, reapply_hash in reapplied:
        print(f"  链上 {chain_hash[:10]}  ←  上游 {upstream[:10]}  {subject}")
        print(f"      回退 {revert_hash} → 再回退 {reapply_hash}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
