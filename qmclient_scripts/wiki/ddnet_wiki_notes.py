#!/usr/bin/env python3
"""把 MediaWiki 的 Special:Export XML 转成可读的 Markdown 笔记。

用法：
    # 1. 用浏览器/curl 下载要参考的页面（不要频繁请求，遵守站点 robots 与 Site Notice）
    #    https://wiki.ddnet.org/wiki/Special:Export?curonly=1&pages=Dummy
    # 2. 把 XML 放进一个目录，再转换
    python qmclient_scripts/wiki/ddnet_wiki_notes.py --input tmp/ddnet_wiki/raw --output tmp/ddnet_wiki/pages

只做“读得懂”的清洗：去掉模板、引用、分类、HTML，保留标题、列表和正文段落。
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def extract(xml: str) -> tuple[str, str]:
    title = re.search(r"<title>(.*?)</title>", xml, re.S)
    text = re.search(r"<text[^>]*>(.*?)</text>", xml, re.S)
    name = title.group(1) if title else "page"
    body = text.group(1) if text else ""
    body = (
        body.replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&amp;", "&")
        .replace("&quot;", '"')
    )
    return name, body


def clean(text: str) -> str:
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"<ref[^>]*>.*?</ref>", "", text, flags=re.S)
    text = re.sub(r"<ref[^>]*/>", "", text)
    text = re.sub(r"<nowiki>(.*?)</nowiki>", r"\1", text, flags=re.S)
    text = re.sub(r"</?[a-zA-Z][^>]*>", "", text)
    for _ in range(3):
        text = re.sub(r"\{\{[^{}]*\}\}", "", text)
    text = re.sub(r"\[\[Category:[^\]]*\]\]", "", text, flags=re.I)
    text = re.sub(r"\[\[[^\]|]*\|([^\]]*)\]\]", r"\1", text)
    text = re.sub(r"\[\[([^\]]*)\]\]", r"\1", text)
    text = re.sub(r"\[https?://\S+\s+([^\]]*)\]", r"\1", text)
    text = text.replace("'''", "**").replace("''", "*")
    text = re.sub(r"^=+\s*(.*?)\s*=+\s*$", lambda m: "## " + m.group(1), text, flags=re.M)
    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(("{|", "|}", "|-", "|", "!")):
            continue
        lines.append(stripped)
    return re.sub(r"\n{3,}", "\n\n", "\n".join(lines)).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="MediaWiki 导出转 Markdown 笔记")
    parser.add_argument("--input", default="tmp/ddnet_wiki/raw")
    parser.add_argument("--output", default="tmp/ddnet_wiki/pages")
    args = parser.parse_args()

    source = Path(args.input)
    target = Path(args.output)
    target.mkdir(parents=True, exist_ok=True)
    count = 0
    for xml_path in sorted(source.glob("*.xml")):
        name, body = extract(xml_path.read_text(encoding="utf-8"))
        if not body:
            continue
        markdown = clean(body)
        (target / (name.replace("/", "_") + ".md")).write_text(markdown, encoding="utf-8")
        print(f"{name}: {len(markdown)} chars")
        count += 1
    print(f"转换 {count} 个页面，输出到 {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
