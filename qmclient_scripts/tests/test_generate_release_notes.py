#!/usr/bin/env python3
"""generate_release_notes 通道、自动润色与渲染的轻量单测（不依赖完整 git 历史）。"""

from __future__ import annotations

import unittest

from qmclient_scripts.generate_release_notes import (
    CommitNote,
    FALLBACK_DOMAIN,
    TYPE_LABEL,
    cleanup_release_text,
    detect_channel,
    normalize_for_dedupe,
    render_domain,
    render_header,
    render_markdown,
)


def _note(
    *,
    commit_type: str,
    release_zh: str,
    domain: str = "界面与视觉",
    scope: str = "ui",
    commit_hash: str = "abc",
) -> CommitNote:
    return CommitNote(
        commit_hash=commit_hash,
        commit_type=commit_type,
        scope=scope,
        description=release_zh,
        release_zh=release_zh,
        domain=domain,
    )


class DetectChannelTests(unittest.TestCase):
    def test_stable_tags(self) -> None:
        self.assertEqual(detect_channel("v3"), "stable")
        self.assertEqual(detect_channel("v3.1"), "stable")
        self.assertEqual(detect_channel("v2.74.9"), "stable")
        self.assertEqual(detect_channel("2.74.9"), "stable")

    def test_pre_release_tags(self) -> None:
        self.assertEqual(detect_channel("nightly"), "pre-release")
        self.assertEqual(detect_channel("v2.0.0-rc1"), "pre-release")
        self.assertEqual(detect_channel("19.9-rc1"), "pre-release")
        self.assertEqual(detect_channel("v3.0.0-beta.1"), "pre-release")


class RenderHeaderTests(unittest.TestCase):
    def test_stable_header_mentions_channel(self) -> None:
        lines = render_header(
            channel="stable",
            version="v2.74.9",
            current_tag="v2.74.9",
            previous_tag="v2.74.8",
            commit=None,
            built_at=None,
            branch=None,
        )
        text = "\n".join(lines)
        self.assertIn("正式版", text)
        self.assertIn("Stable", text)
        self.assertIn("v2.74.8..v2.74.9", text)

    def test_pre_release_header_warns_and_states_auto_polish(self) -> None:
        lines = render_header(
            channel="pre-release",
            version="nightly",
            current_tag="nightly",
            previous_tag="v2.74.9",
            commit="abc123",
            built_at="2026-07-15 00:00:00 UTC",
            branch="main",
        )
        text = "\n".join(lines)
        self.assertIn("预发布", text)
        self.assertIn("不建议当主力", text)
        self.assertIn("说明来源", text)
        self.assertIn("自动汇总并润色", text)
        self.assertIn("无需人工润色", text)
        self.assertIn("Nightly", text)
        self.assertIn("abc123", text)

    def test_empty_notes_markdown(self) -> None:
        md = render_markdown(
            version="v2.74.9",
            current_tag="v2.74.9",
            previous_tag="v2.74.8",
            notes=[],
            channel="stable",
        )
        self.assertIn("正式版", md)
        self.assertIn("完整变更", md)


class TypeLabelTests(unittest.TestCase):
    def test_chinese_player_prefixes(self) -> None:
        self.assertEqual(TYPE_LABEL["feat"], "新增")
        self.assertEqual(TYPE_LABEL["fix"], "修复")
        self.assertEqual(TYPE_LABEL["perf"], "优化")
        self.assertEqual(TYPE_LABEL["improve"], "改进")
        self.assertEqual(TYPE_LABEL["revert"], "回退")


class CleanupAndDedupeHelpersTests(unittest.TestCase):
    def test_cleanup_strips_trailing_period_and_whitespace(self) -> None:
        self.assertEqual(cleanup_release_text("  修复  卡顿。  "), "修复 卡顿")
        self.assertEqual(cleanup_release_text("优化渲染."), "优化渲染")

    def test_near_identical_normalize(self) -> None:
        self.assertEqual(
            normalize_for_dedupe("修复卡顿。"),
            normalize_for_dedupe("修复 卡顿"),
        )


class RenderDomainPolishTests(unittest.TestCase):
    def test_mixed_types_use_chinese_prefix(self) -> None:
        notes = [
            _note(commit_type="feat", release_zh="新增图片压缩"),
            _note(commit_type="fix", release_zh="修复预览闪烁。"),
        ]
        lines = render_domain("界面与视觉", notes)
        text = "\n".join(lines)
        self.assertIn("- 新增：新增图片压缩", text)
        self.assertIn("- 修复：修复预览闪烁", text)
        self.assertNotIn("feat:", text)
        self.assertNotIn("fix:", text)

    def test_same_type_omits_prefix(self) -> None:
        notes = [
            _note(commit_type="feat", release_zh="支持圆形裁剪"),
            _note(commit_type="feat", release_zh="支持批量导出"),
        ]
        lines = render_domain("界面与视觉", notes)
        text = "\n".join(lines)
        self.assertIn("- 支持圆形裁剪", text)
        self.assertIn("- 支持批量导出", text)
        self.assertNotIn("新增：", text)
        self.assertNotIn("feat", text)

    def test_dedupe_identical_and_near_identical(self) -> None:
        notes = [
            _note(commit_type="fix", release_zh="修复语音断连", domain="语音", commit_hash="1"),
            _note(commit_type="fix", release_zh="修复语音断连", domain="语音", commit_hash="2"),
            _note(commit_type="fix", release_zh="修复 语音断连。", domain="语音", commit_hash="3"),
            _note(commit_type="fix", release_zh="修复麦克风权限", domain="语音", commit_hash="4"),
        ]
        lines = render_domain("语音", notes)
        # 同 type 省略前缀；三条近相同只留首条 + 另一条
        bullets = [line for line in lines if line.startswith("- ")]
        self.assertEqual(len(bullets), 2)
        self.assertEqual(bullets[0], "- 修复语音断连")
        self.assertEqual(bullets[1], "- 修复麦克风权限")

    def test_fallback_domain_capped_with_omission_note(self) -> None:
        notes = [
            _note(
                commit_type="fix",
                release_zh=f"其他修复{i}",
                domain=FALLBACK_DOMAIN,
                scope="",
                commit_hash=str(i),
            )
            for i in range(1, 7)
        ]
        lines = render_domain(FALLBACK_DOMAIN, notes)
        bullets = [line for line in lines if line.startswith("- ")]
        # 4 条正文 + 1 条省略提示
        self.assertEqual(len(bullets), 5)
        self.assertTrue(any("另有 2 条未列出" in b for b in bullets))
        self.assertTrue(any("见完整变更" in b for b in bullets))
        listed = [b for b in bullets if "另有" not in b]
        self.assertEqual(len(listed), 4)

    def test_stable_markdown_also_polished(self) -> None:
        notes = [
            _note(commit_type="perf", release_zh="降低设置页滚动开销。"),
            _note(commit_type="fix", release_zh="修复窄窗双列布局"),
        ]
        md = render_markdown(
            version="v2.74.9",
            current_tag="v2.74.9",
            previous_tag="v2.74.8",
            notes=notes,
            channel="stable",
        )
        self.assertIn("## 界面与视觉", md)
        self.assertIn("- 优化：降低设置页滚动开销", md)
        self.assertIn("- 修复：修复窄窗双列布局", md)


if __name__ == "__main__":
    unittest.main()
