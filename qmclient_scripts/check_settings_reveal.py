#!/usr/bin/env python3
"""设置页「条件行必须带动画」静态闸门。

规则：设置卡片的行声明只能来自 CSettingsContentLayoutFlow（Flow.Control /
Flow.Text / Flow.TextColumns 等）。这类行如果被 g_Config 的布尔开关直接包住，
展开/收起时高度会瞬变（内容先跳、行后让位）。凡是这种 if 块，必须同一作用域内
出现 CSettingsConditionalRows 或 CSettingsRevealScope / CSettingsContentRevealScope，
否则报违规。

用法：python qmclient_scripts/check_settings_reveal.py [--list]
退出码：0 通过；1 有违规。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CARDS = ROOT / "src" / "game" / "client" / "QmUi"
ROW_CALL = re.compile(r"\bFlow\.(Control|Text|TextColumns|MeasuredControl)\(")
# 卡片内的行 helper：它们内部才 new Flow（或接收卡片的 Flow），按大括号配对看不到，
# 所以「单语句 if(g_Config...) Helper(...)」要单独认出来，否则这类条件行永远逃过检查。
ROW_HELPER_CALL = re.compile(r"\b(NextRow|NumericField|CheckBox|DeclaredCheckbox|ToggleRow|ColorPickerRow|RadioRow)\s*\(")
CONFIG_IF = re.compile(r"^\s*if\s*\(\s*g_Config\.m_[A-Za-z0-9_]+\s*\)")
REVEAL = re.compile(r"CSettingsConditionalRows|CSettingsRevealScope|CSettingsContentRevealScope|BeginReveal\(")
# 这些开关驱动的是整卡/整页开关或呈现方式，不是行显隐，允许无动画。
# 键是「相对仓库根的 posix 路径:if 起始行」，值里的 marker 必须仍然出现在该行上：
# 行号漂移或那一行被改动后条目立刻失效并重新报违规，不留一条永远沉默的豁免。
ALLOW_IDS = {
    "src/game/client/QmUi/cards/QmCardCatalogTitle.cpp:273": (
        "g_Config.m_QmTitleInstantHints",
        "只切换悬浮提示的呈现方式（即时小提示 / 延迟提示），既不声明也不收起任何行；"
        "扫描器按大括号配对，会把紧跟其后的 Flow.Text 称号状态行一并算进 if 范围。",
    ),
}


def scan(path: Path):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    problems = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if CONFIG_IF.match(line):
            # 单语句条件行：if(g_Config...) Helper(...); —— 没有大括号，按到分号为止取 body。
            if "{" not in line:
                body = [line]
                depth = line.count("(") - line.count(")")
                j = i
                while depth > 0 and j + 1 < len(lines):
                    j += 1
                    body.append(lines[j])
                    depth += lines[j].count("(") - lines[j].count(")")
                text = "\n".join(body)
                if ROW_HELPER_CALL.search(text) and not REVEAL.search(text):
                    problems.append((i + 1, line.strip()[:90], j + 1))
                i = j + 1
                continue
            # 找到块结束（按大括号配对）
            depth = 0
            started = False
            body = []
            j = i
            while j < len(lines):
                depth += lines[j].count("{") - lines[j].count("}")
                if "{" in lines[j]:
                    started = True
                body.append(lines[j])
                if started and depth <= 0:
                    break
                j += 1
            text = "\n".join(body)
            if ROW_CALL.search(text) and not REVEAL.search(text):
                problems.append((i + 1, line.strip()[:90], j + 1))
            i = j + 1
            continue
        i += 1
    return problems


def list_allowed() -> int:
    for key, (marker, reason) in sorted(ALLOW_IDS.items()):
        print(f"{key}  [{marker}]")
        print(f"    {reason}")
    print(f"\n共 {len(ALLOW_IDS)} 条登记。")
    return 0


def main() -> int:
    if "--list" in sys.argv[1:]:
        return list_allowed()
    files = sorted(CARDS.glob("cards/*.cpp")) + sorted(CARDS.glob("*.cpp"))
    total = 0
    allowed = 0
    for f in files:
        rel = f.relative_to(ROOT).as_posix()
        for start, snippet, end in scan(f):
            key = f"{rel}:{start}"
            if key in ALLOW_IDS:
                marker, _reason = ALLOW_IDS[key]
                if marker not in snippet:
                    total += 1
                    print(f"{rel}:{start} ALLOW_IDS 条目已失效（该行不再含 {marker}），请更新登记: {snippet}")
                    continue
                allowed += 1
                continue
            total += 1
            print(f"{rel}:{start}-{end} 条件行没有展开动画: {snippet}")
    if total:
        print(f"\n共 {total} 处。给这些块加 CSettingsConditionalRows + CSettingsContentRevealScope；")
        print("确实不是行显隐的，在 ALLOW_IDS 里登记行号和理由（--list 查看已登记项）。")
        return 1
    print(f"设置页条件行动画检查通过（已登记跳过 {allowed} 处）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
