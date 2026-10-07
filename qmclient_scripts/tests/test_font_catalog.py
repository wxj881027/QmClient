#!/usr/bin/env python3
"""字体商店目录契约：data/fonts/fonts_store/font_catalog.json 结构、路径安全与体量预算。

目录由 C++ 侧 json-parser 读取（不支持注释/尾逗号），仓库路径在运行时拼接为
ghproxy.net / ghfast.top / raw.githubusercontent.com 下载链，基名直接落盘到
fonts/fonts_store 与 qmclient/fontcache，这里锁住结构与路径安全约束。
"""

from pathlib import Path
import json
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = REPO_ROOT / "data" / "fonts" / "fonts_store" / "font_catalog.json"
MAX_CATALOG_BYTES = 700 * 1024
# google/fonts 仓库相对路径：许可目录/族目录/文件名，且族目录段不得是 static（可变字体已覆盖）。
PATH_RE = re.compile(r"^(?:ofl|apache|ufl)/(?!static/)[a-z0-9_\-]+/[A-Za-z0-9_\[\](),\-]+\.ttf$")


class FontCatalogContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = CATALOG_PATH.read_text(encoding="utf-8")
        # json-parser 兼容性前置检查：注释和尾逗号都会让 C++ 侧解析失败。
        comment_lines = [
            line
            for line in cls.raw.splitlines()
            if line.lstrip().startswith("//") or line.lstrip().startswith("/*")
        ]
        assert not comment_lines, f"目录不允许注释（json-parser 不支持）：{comment_lines[:3]}"
        assert "/*" not in cls.raw.replace("://", "＃")
        cls.catalog = json.loads(cls.raw)

    def test_size_budget(self) -> None:
        # 目录随包发行：超过预算说明生成器把不该带的数据（如 static/ 子目录）收进来了。
        self.assertLessEqual(CATALOG_PATH.stat().st_size, MAX_CATALOG_BYTES)

    def test_structure(self) -> None:
        self.assertIsInstance(self.catalog.get("version"), int)
        fonts = self.catalog.get("fonts")
        self.assertIsInstance(fonts, list)
        self.assertGreater(len(fonts), 1000, "目录应覆盖 Google Fonts 全量字体族")

    def test_entries_have_safe_fields(self) -> None:
        seen_pairs, seen_files, families = set(), set(), set()
        for entry in self.catalog["fonts"]:
            with self.subTest(entry=entry.get("f")):
                name = entry.get("n")
                self.assertIsInstance(name, str)
                self.assertTrue(name.strip())
                families.add(name.lower())

                path = entry.get("f")
                self.assertIsInstance(path, str)
                # 路径会拼进下载 URL 与用户存储路径：锁死仓库相对路径形态。
                self.assertRegex(path, PATH_RE)
                self.assertNotIn("..", path)

                base = path.rsplit("/", 1)[-1]
                self.assertNotIn(base, seen_files, "目标文件名重复会互相覆盖")
                seen_files.add(base)

                pair = (name.lower(), path)
                self.assertNotIn(pair, seen_pairs, "同一族同一文件重复")
                seen_pairs.add(pair)

                if "v" in entry:
                    self.assertIn(entry["v"], (0, 1))

                # 国内直链锁死 gstatic.cn 域与 TTF 后缀（客户端同样只接受该形态）。
                if "u" in entry:
                    self.assertTrue(entry["u"].startswith("https://fonts.gstatic.cn/"), entry["u"])
                    self.assertTrue(entry["u"].endswith(".ttf"), entry["u"])
                    self.assertNotIn("v", entry, "可变字体不得挂直链（镜像只返回静态实例）")

    def test_direct_url_coverage(self) -> None:
        """国内直链覆盖率与 CJK 排除：镜像对 CJK 只返回拉丁子集，必须缺席。"""
        entries = self.catalog["fonts"]
        direct = [entry for entry in entries if "u" in entry]
        self.assertGreater(len(direct), 300, "静态单文件拉丁族应大量携带 gstatic 直链")
        for entry in entries:
            if entry["n"] in ("Noto Sans SC", "Noto Serif SC", "LXGW WenKai TC"):
                self.assertNotIn("u", entry, f"{entry['n']} 是 CJK 族，不得携带镜像直链")

    def test_key_fonts_present(self) -> None:
        families = {entry["n"].lower() for entry in self.catalog["fonts"]}
        for required in ("jetbrains mono", "noto sans sc", "noto serif sc", "jetbrains mono"):
            self.assertIn(required, families)
        # 目录里的路径必须真实存在于 google/fonts 仓库主分支：抽查可变字体直链可用性
        # 由 generate_font_catalog.py 的生成流程保证，这里只锁关键族在列。
        self.assertIn("lxgw wenkai tc", families)

    def test_variable_flag_consistency(self) -> None:
        for entry in self.catalog["fonts"]:
            if "v" in entry and entry["v"]:
                self.assertRegex(entry["f"], r"\[[^\]]*wght[^\]]*\]\.ttf$")

    def test_script_subsets(self) -> None:
        """脚本子集 "s" 字段：白名单值、关键多脚本族覆盖、仅拉丁族不写。"""
        whitelist = {"arabic", "chinese-simplified", "chinese-traditional", "cyrillic", "greek", "japanese", "korean"}
        subsets_by_family: dict[str, set[str]] = {}
        for entry in self.catalog["fonts"]:
            subsets = entry.get("s")
            if subsets is None:
                continue
            with self.subTest(entry=entry.get("f")):
                self.assertIsInstance(subsets, list)
                self.assertTrue(subsets, "空子集列表应省略字段而不是写空数组")
                self.assertLessEqual(set(subsets), whitelist, "超出白名单的子集键（运行时无法映射语言）")
            subsets_by_family.setdefault(entry["n"], set()).update(subsets)

        for family, expect in (
            ("Noto Sans SC", {"chinese-simplified"}),
            ("Noto Sans JP", {"japanese"}),
            ("Noto Sans KR", {"korean"}),
            ("JetBrains Mono", {"cyrillic", "greek"}),
        ):
            self.assertIn(family, subsets_by_family, f"{family} 应携带脚本子集")
            self.assertTrue(subsets_by_family[family] & expect, f"{family} 缺少期望子集 {expect}")

        # 西里尔覆盖量要够俄语用户挑选；仅拉丁族（如 Abel）不得携带 "s"。
        cyrillic_families = {name for name, subsets in subsets_by_family.items() if "cyrillic" in subsets}
        self.assertGreater(len(cyrillic_families), 100)
        abel = [entry for entry in self.catalog["fonts"] if entry["n"] == "Abel"]
        if abel:
            self.assertNotIn("s", abel[0])


if __name__ == "__main__":
    unittest.main()
