# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""Chinese typography helpers for QmClient translations."""

from __future__ import annotations

import re

CJK_RE = r"\u3400-\u4dbf\u4e00-\u9fff"
ASCII_WORD_RE = r"A-Za-z0-9"


def normalize_simplified_chinese_text(text: str) -> str:
    """Apply conservative Chinese copywriting spacing to UI translations."""

    if not text:
        return text

    # 标题两侧成对的装饰标点保持原样，正文仍按中文排版规范处理。
    decorated_title = text.startswith("?!") and text.endswith("!?") and len(text) >= 4
    normalized = text[2:-2] if decorated_title else text

    has_cjk = re.search(rf"[{CJK_RE}]", normalized) is not None

    # Chinese translations should use full-width sentence punctuation, including
    # mixed endings such as "QmClient!!!" in an otherwise Chinese sentence.
    if has_cjk:
        normalized = re.sub(r"!+", lambda match: "！" * len(match.group(0)), normalized)
        normalized = re.sub(r"\.{3,}", "……", normalized)
        normalized = re.sub(
            rf"(?<=[{CJK_RE}])\?+",
            lambda match: "？" * len(match.group(0)),
            normalized,
        )
        normalized = re.sub(
            rf"\?+(?=[{CJK_RE}])",
            lambda match: "？" * len(match.group(0)),
            normalized,
        )
    normalized = re.sub(rf"(?<=[{CJK_RE}]),", "，", normalized)
    normalized = re.sub(rf",(?=[{CJK_RE}])", "，", normalized)
    normalized = re.sub(rf"(?<=[{CJK_RE}]):", "：", normalized)

    # Add spaces between CJK and ASCII words, numbers, commands, and placeholders.
    normalized = re.sub(rf"(?<=[{CJK_RE}])(?=[{ASCII_WORD_RE}%/])", " ", normalized)
    normalized = re.sub(rf"(?<=[{ASCII_WORD_RE}%])(?=[{CJK_RE}])", " ", normalized)

    # Normalize accidental runs introduced around existing spaces.
    normalized = re.sub(r"[ \t]{2,}", " ", normalized)
    normalized = re.sub(rf"(?<=[{CJK_RE}])\s*/\s*(?=[{CJK_RE}])", "/", normalized)
    normalized = re.sub(r"(/[\w-]+)？(?=[A-Za-z])", r"\1 ?", normalized)
    normalized = re.sub(r"(?<=[A-Za-z0-9])？(?=[A-Za-z0-9])", "?", normalized)
    normalized = re.sub(r" +([，。！？；：、）])", r"\1", normalized)
    normalized = re.sub(r"([，。！？；、]) +", r"\1", normalized)
    normalized = re.sub(r"([：]) +", r"\1", normalized)
    normalized = re.sub(r"([（]) +", r"\1", normalized)

    return f"?!{normalized}!?" if decorated_title else normalized
