# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""
根据 tag / ref 区间内的 commit 生成 GitHub Release 说明。

通道：
- stable（正式版）：tag 形如 vX、vX.Y 或 vX.Y.Z，对应 GitHub 正式 Release
- pre-release（预发布）：nightly / rc / beta 等，对应 GitHub Pre-release

输出按「功能领域」分组、中文优先，并对条目做确定性自动润色：
- Conventional type → 中文玩家前缀（feat→新增 等）
- 同领域性质单一时省略前缀
- 同领域相同/近相同 release_zh 去重
- 「其他」上限 4 条并附未列出提示
- 轻量文案清理（空白折叠、去句末标点）

工程类 commit（ci/build/gate 等）解析但不渲染。
优先读取 commit body：

    Release-ZH: 中文发布说明

缺失时回退 subject 描述。

Nightly 终稿即本脚本输出（无需人工润色）；正式版可选手动再改。
规范见 docs/RELEASE_NOTE_TEMPLATE.md。
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
COMMIT_SEPARATOR = "\x1e"
FIELD_SEPARATOR = "\x1f"

SUBJECT_RE = re.compile(
    r"^(?P<type>[A-Za-z]+)(?:\((?P<scope>[^)]+)\))?(?P<breaking>!)?[:：]\s*(?P<desc>.+)$"
)
TRAILER_RE = re.compile(r"^Release-ZH:\s*(?P<text>.+)$")
# 正式版 tag：v3 / v3.1 / v3.1.1（不含 rc/beta/nightly 后缀）
STABLE_TAG_RE = re.compile(r"^v?(?P<ver>\d+(?:\.\d+){0,2})$")
PRE_RELEASE_HINT_RE = re.compile(
    r"(?i)(nightly|rc\d*|alpha|beta|pre|preview|snapshot|dev)"
)

# 玩家可感知的 commit type。
# refactor 默认不进发布说明（实现重构玩家无感）；可用 --include-refactor 打开。
PLAYER_TYPES = {"feat", "fix", "perf", "improve", "revert"}
OPTIONAL_PLAYER_TYPES = {"refactor"}

# Conventional type → 玩家向中文前缀（确定性，不调用外部 AI）
TYPE_LABEL: dict[str, str] = {
    "feat": "新增",
    "fix": "修复",
    "perf": "优化",
    "improve": "改进",
    "revert": "回退",
    "refactor": "重构",
}

# 组内排序：feat 在前，revert 在后
TYPE_PRIORITY = {
    "feat": 0,
    "improve": 1,
    "fix": 2,
    "perf": 3,
    "refactor": 4,
    "revert": 5,
}

# 「其他」领域展示上限（超出后追加省略提示）
FALLBACK_DOMAIN_CAP = 4

DOMAIN_ORDER = [
    "界面与视觉",
    "聊天与社交",
    "翻译与语言",
    "语音",
    "输入与操作",
    "媒体与渲染",
    "设置页",
    "平台适配",
    "客户端与核心",
    "其他",
]

INTERNAL_DOMAIN = "开发者与构建"
FALLBACK_DOMAIN = "其他"

SCOPE_TO_DOMAIN: dict[str, str] = {
    "ui": "界面与视觉",
    "hud": "界面与视觉",
    "qmhud": "界面与视觉",
    "hud-editor": "界面与视觉",
    "nameplate": "界面与视觉",
    "nameplates": "界面与视觉",
    "scoreboard": "界面与视觉",
    "menus": "界面与视觉",
    "menu": "界面与视觉",
    "menus_demo": "界面与视觉",
    "pie_menu": "界面与视觉",
    "particles": "界面与视觉",
    "skins": "界面与视觉",
    "entity-bg": "界面与视觉",
    "gores": "界面与视觉",
    "image": "界面与视觉",
    "assets": "界面与视觉",
    "资源页面": "界面与视觉",
    "chat": "聊天与社交",
    "聊天交互": "聊天与社交",
    "friends": "聊天与社交",
    "spectator": "聊天与社交",
    "console": "聊天与社交",
    "通知栏": "聊天与社交",
    "translate": "翻译与语言",
    "i18n": "翻译与语言",
    "翻译模块": "翻译与语言",
    "macos与翻译": "翻译与语言",
    "ime": "翻译与语言",
    "agents": "翻译与语言",
    "voice": "语音",
    "语音": "语音",
    "语音模块": "语音",
    "voicesrv": "语音",
    "input": "输入与操作",
    "input-overlay": "输入与操作",
    "bind": "输入与操作",
    "fast-practice": "输入与操作",
    "practice": "输入与操作",
    "lyrics": "媒体与渲染",
    "sound": "媒体与渲染",
    "vulkan": "媒体与渲染",
    "live": "媒体与渲染",
    "game": "媒体与渲染",
    "modes": "媒体与渲染",
    "collision": "媒体与渲染",
    "trajectory": "媒体与渲染",
    "player_points": "媒体与渲染",
    "settings": "设置页",
    "settings-ui": "设置页",
    "config": "设置页",
    "spec": "设置页",
    "macos": "平台适配",
    "linux": "平台适配",
    "windows": "平台适配",
    "android": "平台适配",
    "qmclient": "客户端与核心",
    "client": "客户端与核心",
    "客户端": "客户端与核心",
    "tclient": "客户端与核心",
    "core": "客户端与核心",
    "qimeng": "客户端与核心",
    "qmrt": "客户端与核心",
    "section-loader": "客户端与核心",
    "sync": "客户端与核心",
    "demo": "客户端与核心",
    "editor": "客户端与核心",
    "perf": "客户端与核心",
    "ci": INTERNAL_DOMAIN,
    "build": INTERNAL_DOMAIN,
    "gate": INTERNAL_DOMAIN,
    "clang-tidy": INTERNAL_DOMAIN,
    "git": INTERNAL_DOMAIN,
    "workflow": INTERNAL_DOMAIN,
    "version": INTERNAL_DOMAIN,
    "tooling": INTERNAL_DOMAIN,
    "tidy": INTERNAL_DOMAIN,
    "strict-build": INTERNAL_DOMAIN,
    "asan": INTERNAL_DOMAIN,
    "nightly": INTERNAL_DOMAIN,
    "eol": INTERNAL_DOMAIN,
    "gitignore": INTERNAL_DOMAIN,
    "ddnet-libs": INTERNAL_DOMAIN,
    "github": INTERNAL_DOMAIN,
    "wiki": INTERNAL_DOMAIN,
    "pr": INTERNAL_DOMAIN,
    "pr156": INTERNAL_DOMAIN,
    "merge": INTERNAL_DOMAIN,
    "master": INTERNAL_DOMAIN,
    "main": INTERNAL_DOMAIN,
    "monitoring": INTERNAL_DOMAIN,
    "logging": INTERNAL_DOMAIN,
    "explore": INTERNAL_DOMAIN,
    "plan": INTERNAL_DOMAIN,
    "backlog": INTERNAL_DOMAIN,
    "test": INTERNAL_DOMAIN,
    "ai": INTERNAL_DOMAIN,
    "scripts": INTERNAL_DOMAIN,
    "timeout": INTERNAL_DOMAIN,
    "all": INTERNAL_DOMAIN,
}

_WARNINGS: list[tuple[str, str]] = []


@dataclass
class CommitNote:
    commit_hash: str
    commit_type: str
    scope: str
    description: str
    release_zh: str
    domain: str

    def format_prefix(self) -> str:
        """调试/兼容用：英文 conventional 前缀。渲染玩家文案请用 format_player_prefix。"""
        if self.scope:
            return f"{self.commit_type}({self.scope})"
        return self.commit_type

    def format_player_prefix(self) -> str:
        return TYPE_LABEL.get(self.commit_type, self.commit_type)


def run_git(args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout.strip()


def git_ref_exists(ref: str) -> bool:
    result = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", ref],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def detect_channel(tag: str) -> str:
    """返回 stable 或 pre-release。"""
    name = tag.strip()
    if not name:
        return "pre-release"
    if name == "nightly" or name.startswith("nightly/"):
        return "pre-release"
    if PRE_RELEASE_HINT_RE.search(name):
        return "pre-release"
    if STABLE_TAG_RE.match(name):
        return "stable"
    # 无法识别时按预发布处理，避免误标正式版
    return "pre-release"


def resolve_domain(scope: str, commit_hash: str) -> str:
    key = scope.strip().lower()
    if not key:
        return FALLBACK_DOMAIN
    if key in SCOPE_TO_DOMAIN:
        return SCOPE_TO_DOMAIN[key]
    _WARNINGS.append((scope, commit_hash))
    return FALLBACK_DOMAIN


def parse_trailers(body: str) -> str:
    for line in body.splitlines():
        match = TRAILER_RE.match(line.strip())
        if match:
            return match.group("text").strip()
    return ""


def parse_commit(
    commit_hash: str,
    subject: str,
    body: str,
    player_types: set[str] | None = None,
) -> CommitNote | None:
    match = SUBJECT_RE.match(subject.strip())
    if not match:
        return None
    commit_type = match.group("type").lower()
    allowed = player_types if player_types is not None else PLAYER_TYPES
    if commit_type not in allowed:
        return None
    scope = (match.group("scope") or "").strip()
    description = match.group("desc").strip()
    release_zh = parse_trailers(body) or description
    domain = resolve_domain(scope, commit_hash)
    if domain == INTERNAL_DOMAIN:
        return None
    return CommitNote(
        commit_hash=commit_hash,
        commit_type=commit_type,
        scope=scope,
        description=description,
        release_zh=release_zh,
        domain=domain,
    )


def cleanup_release_text(text: str) -> str:
    """轻量清理：折叠空白、去掉句末句号/点号。"""
    cleaned = re.sub(r"\s+", " ", (text or "").strip())
    cleaned = re.sub(r"[。.．.]+$", "", cleaned).strip()
    return cleaned


def normalize_for_dedupe(text: str) -> str:
    """近相同判定：清理后再去掉常见标点与空白差异。"""
    cleaned = cleanup_release_text(text)
    return re.sub(r"[\s，,。．.!！?？:：;；、]+", "", cleaned).casefold()


def dedupe_notes(notes: list[CommitNote]) -> list[CommitNote]:
    """同领域内按近相同 release_zh 去重，保留首次出现。"""
    seen: set[str] = set()
    result: list[CommitNote] = []
    for note in notes:
        key = normalize_for_dedupe(note.release_zh)
        if not key or key in seen:
            continue
        seen.add(key)
        result.append(note)
    return result


def format_player_bullet(note: CommitNote, *, omit_prefix: bool) -> str:
    text = cleanup_release_text(note.release_zh)
    if omit_prefix:
        return f"- {text}"
    return f"- {note.format_player_prefix()}：{text}"


def list_tags_by_creatordate() -> list[str]:
    raw = run_git(["tag", "--sort=-creatordate"])
    return [line.strip() for line in raw.splitlines() if line.strip()]


def list_stable_tags() -> list[str]:
    return [tag for tag in list_tags_by_creatordate() if STABLE_TAG_RE.match(tag)]


def resolve_previous_tag(current_tag: str, channel: str) -> str | None:
    """正式版：同通道上一 tag；预发布：相对最近正式版，便于玩家看「相对 stable 多了什么」。"""
    if channel == "pre-release":
        stables = list_stable_tags()
        for tag in stables:
            if tag != current_tag:
                return tag
        return None

    tags = run_git(["tag", "--sort=-creatordate", "--merged", current_tag]).splitlines()
    for tag in tags:
        tag = tag.strip()
        if tag and tag != current_tag and STABLE_TAG_RE.match(tag):
            return tag
    for tag in tags:
        tag = tag.strip()
        if tag and tag != current_tag:
            return tag
    return None

def collect_notes(
    current_tag: str,
    previous_tag: str | None,
    player_types: set[str] | None = None,
) -> list[CommitNote]:
    revspec = f"{previous_tag}..{current_tag}" if previous_tag else current_tag
    raw = run_git(
        [
            "log",
            "--reverse",
            f"--format=%H{FIELD_SEPARATOR}%s{FIELD_SEPARATOR}%b{COMMIT_SEPARATOR}",
            revspec,
        ]
    )
    notes: list[CommitNote] = []
    for entry in raw.split(COMMIT_SEPARATOR):
        if not entry.strip():
            continue
        parts = entry.split(FIELD_SEPARATOR, 2)
        if len(parts) != 3:
            continue
        note = parse_commit(
            parts[0].strip(),
            parts[1].strip(),
            parts[2],
            player_types=player_types,
        )
        if note is not None:
            notes.append(note)
    return notes


def render_domain(domain: str, notes: list[CommitNote]) -> list[str]:
    grouped = [note for note in notes if note.domain == domain]
    if not grouped:
        return []
    grouped.sort(key=lambda note: TYPE_PRIORITY.get(note.commit_type, 99))
    grouped = dedupe_notes(grouped)
    if not grouped:
        return []

    omitted = 0
    if domain == FALLBACK_DOMAIN and len(grouped) > FALLBACK_DOMAIN_CAP:
        omitted = len(grouped) - FALLBACK_DOMAIN_CAP
        grouped = grouped[:FALLBACK_DOMAIN_CAP]

    types = {note.commit_type for note in grouped}
    omit_prefix = len(types) == 1

    lines = [f"## {domain}"]
    for note in grouped:
        lines.append(format_player_bullet(note, omit_prefix=omit_prefix))
    if omitted:
        lines.append(f"- …另有 {omitted} 条未列出，见完整变更")
    lines.append("")
    return lines



def render_header(
    *,
    channel: str,
    version: str,
    current_tag: str,
    previous_tag: str | None,
    commit: str | None,
    built_at: str | None,
    branch: str | None,
) -> list[str]:
    lines: list[str] = []
    if channel == "stable":
        lines.append(f"# QmClient {version} · 正式版（Stable）")
        lines.append("")
        lines.append("> **通道**：正式发布 · 建议日常使用")
        lines.append(f"> **Tag**：`{current_tag}`")
    else:
        title = "Nightly" if current_tag == "nightly" else version
        lines.append(f"# QmClient {title} · 预发布（Pre-release）")
        lines.append("")
        lines.append("> **通道**：预发布 / 内部测试 · **Nightly 构建**")
        lines.append("> **注意**：可能不稳定；`nightly` 会被下一次构建覆盖，**不建议当主力客户端**")
        lines.append(
            "> **说明来源**：由区间内 commit **自动汇总并润色**"
            "（中文前缀 / 同质省略前缀 / 去重 / 「其他」上限；"
            "含 `feat`/`fix`/`perf`/`improve`/`revert`，默认不含 `refactor`/工程类；"
            "无需人工润色）"
        )
        lines.append(f"> **Tag**：`{current_tag}`")
        if branch:
            lines.append(f"> **分支**：`{branch}`")
        if commit:
            lines.append(f"> **Commit**：`{commit}`")
        if built_at:
            lines.append(f"> **构建时间**：{built_at}")

    if previous_tag:
        lines.append(f"> **范围**：`{previous_tag}..{current_tag}`")
    else:
        lines.append(f"> **范围**：`{current_tag}`")
    lines.append("")
    lines.append("---")
    lines.append("")
    return lines


def render_markdown(
    *,
    version: str,
    current_tag: str,
    previous_tag: str | None,
    notes: list[CommitNote],
    channel: str,
    commit: str | None = None,
    built_at: str | None = None,
    branch: str | None = None,
) -> str:
    lines = render_header(
        channel=channel,
        version=version,
        current_tag=current_tag,
        previous_tag=previous_tag,
        commit=commit,
        built_at=built_at,
        branch=branch,
    )
    if not notes:
        if channel == "pre-release":
            lines.append("## 说明")
            lines.append("- 本构建相对最近正式版未解析到玩家向提交，或历史深度不足；请以安装包与 commit 为准。")
            lines.append("")
        else:
            lines.append("## 说明")
            lines.append("- 本区间未解析到面向玩家的提交（或仅有工程类变更）。")
            lines.append("")
    else:
        for domain in DOMAIN_ORDER:
            lines.extend(render_domain(domain, notes))

    lines.append("## 完整变更")
    if previous_tag:
        lines.append(
            f"- 对比：`{previous_tag}...{current_tag}`（GitHub Compare / `git log`）"
        )
    else:
        lines.append(f"- 起点：`{current_tag}`")
    lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def emit_warnings() -> None:
    if not _WARNINGS:
        return
    print(
        "[release-notes] 以下 scope 未在映射表中，已归入「其他」，"
        "可考虑补充 SCOPE_TO_DOMAIN：",
        file=sys.stderr,
    )
    for scope, commit_hash in _WARNINGS:
        print(f"  - {scope} (commit {commit_hash[:12]})", file=sys.stderr)


def resolve_repo_path(path: Path | None) -> Path | None:
    if path is None:
        return None
    if path.is_absolute():
        return path
    return REPO_ROOT / path


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 GitHub Release 说明（正式版 / 预发布）")
    parser.add_argument("--version", default="UNRELEASED", help="展示用版本号")
    parser.add_argument("--current-tag", required=True, help="当前 tag/ref，如 v2.74.9 或 nightly")
    parser.add_argument("--previous-tag", default=None, help="上一个 tag；不传则按通道自动推断")
    parser.add_argument(
        "--channel",
        choices=("auto", "stable", "pre-release"),
        default="auto",
        help="发布通道；auto 根据 tag 名推断（vX[.Y[.Z]]=stable，nightly/rc=pre-release）",
    )
    parser.add_argument("--commit", default=None, help="预发布展示用完整 commit SHA")
    parser.add_argument("--branch", default=None, help="预发布展示用分支名")
    parser.add_argument(
        "--built-at",
        default=None,
        help="预发布构建时间（UTC 字符串）；不传则不写",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="输出 Markdown 路径；不传则打印到 stdout",
    )
    parser.add_argument(
        "--include-refactor",
        action="store_true",
        help="把 refactor 提交也写入发布说明（默认排除）",
    )
    args = parser.parse_args()

    channel = detect_channel(args.current_tag) if args.channel == "auto" else args.channel

    if not git_ref_exists(args.current_tag):
        print(
            f"错误：当前 tag/ref 不存在：{args.current_tag}。"
            "CI 场景请传真实 tag；本地预演请改用仓库里已存在的 tag。",
            file=sys.stderr,
        )
        return 2
    if args.previous_tag and not git_ref_exists(args.previous_tag):
        print(f"错误：上一个 tag/ref 不存在：{args.previous_tag}", file=sys.stderr)
        return 2

    previous_tag = args.previous_tag or resolve_previous_tag(args.current_tag, channel)
    output_path = resolve_repo_path(args.output)
    player_types = set(PLAYER_TYPES)
    if args.include_refactor:
        player_types |= OPTIONAL_PLAYER_TYPES
    notes = collect_notes(args.current_tag, previous_tag, player_types=player_types)
    markdown = render_markdown(
        version=args.version,
        current_tag=args.current_tag,
        previous_tag=previous_tag,
        notes=notes,
        channel=channel,
        commit=args.commit,
        built_at=args.built_at,
        branch=args.branch,
    )

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(markdown, encoding="utf-8")
        print(f"已写入发布说明：{output_path}（通道={channel}）")
    else:
        print(markdown, end="")

    emit_warnings()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
