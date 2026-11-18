#!/usr/bin/env python3
"""设置页 UI 字面量预算闸门。

目的：把「间距 / 字号 / 圆角」的裸字面量当成**可度量的债务**。不是一次性清扫，
而是让数字只能减少、不能增加——否则每次改动都会悄悄多出几个 5.0f / 12.0f。

规则（只统计这些位置，避免把颜色、比例、物理常量误算）：
  1. Flow.Control / Flow.Text / Flow.TextColumns 的行距参数是裸数字；
  2. DoLabel / DoLabel_QmIcon / DoMenuLabelStreamed 的**字号参数**是裸数字；
  3. DrawRoundedSurface / DoNextItem / DrawButtonSurface 的**圆角参数**是裸数字；
  4. Flow.Space 的间距是裸数字。

用法：
  python qmclient_scripts/check_ui_literals.py            # 校验（超过预算退出 1）
  python qmclient_scripts/check_ui_literals.py --report   # 只打印明细
  python qmclient_scripts/check_ui_literals.py --update   # 以当前值为新预算（谨慎）
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCAN_DIRS = [ROOT / "src" / "game" / "client" / "QmUi"]
BUDGET_PATH = ROOT / "qmclient_scripts" / "ui_literal_budget.json"

NUMBER = r"-?\d+(?:\.\d+)?f?"
RULES = [
    # (名称, 正则, 取第几个捕获组当数值组)
    ("row-spacing", re.compile(r"\bFlow\.(?:Control|Text|TextColumns)\s*\(.*?,\s*(" + NUMBER + r")\s*\)\s*;"), 1),
    ("space", re.compile(r"\bFlow\.Space\s*\(\s*(" + NUMBER + r")\s*\)\s*;"), 1),
    ("font", re.compile(r"\b(?:DoLabel|DoLabel_QmIcon|DoMenuLabelStreamed)\s*\(.*?,\s*(" + NUMBER + r")\s*,\s*TEXTALIGN"), 1),
    ("radius", re.compile(r"\b(?:DrawRoundedSurface|DoNextItem|DrawButtonSurface)\s*\(.*?,\s*(" + NUMBER + r")\s*\)\s*;"), 1),
]


def scan() -> dict[str, int]:
    counts: dict[str, int] = {}
    for d in SCAN_DIRS:
        for f in sorted(d.rglob("*.cpp")):
            hits = 0
            for line in f.read_text(encoding="utf-8", errors="replace").splitlines():
                stripped = line.strip()
                if stripped.startswith("//"):
                    continue
                for _name, pattern, _group in RULES:
                    if pattern.search(line):
                        hits += 1
                        break
            if hits:
                counts[f.relative_to(ROOT).as_posix()] = hits
    return counts


def main() -> int:
    args = sys.argv[1:]
    counts = scan()
    total = sum(counts.values())
    if "--report" in args:
        for path, n in sorted(counts.items(), key=lambda kv: -kv[1]):
            print(f"{n:5d}  {path}")
        print(f"\n合计 {total} 处裸字面量（间距 / 字号 / 圆角）。")
        return 0
    if "--update" in args:
        BUDGET_PATH.write_text(json.dumps({"total": total, "files": counts}, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"已把预算更新为 {total}。")
        return 0
    budget = json.loads(BUDGET_PATH.read_text(encoding="utf-8")) if BUDGET_PATH.exists() else {"total": total}
    allowed = int(budget.get("total", total))
    if total > allowed:
        print(f"裸字面量变多了：{allowed} -> {total}")
        for path, n in sorted(counts.items(), key=lambda kv: -kv[1])[:10]:
            print(f"{n:5d}  {path}")
        print("请改用 ui_token::settings::{RowGap,SubRowGap,SectionGap,ActionGap} / Metrics.m_BodySize / ui_token::radius::*。")
        return 1
    print(f"设置页 UI 字面量预算通过：{total} / {allowed}（只减不增）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
