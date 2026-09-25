# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""生成 README 展示用的图表 SVG:star 增长曲线与提交活动图。

本仓库由 DDNet fork 而来,Git 历史包含两万余条上游提交(最早可追到 2007 年
的 Teeworlds)。因此提交活动图必须按 QmClient 贡献者邮箱过滤,否则画出来的是
上游十几年的活动,与项目本身无关。

输出为手写 SVG,不依赖任何绘图库;star 数据取自 GitHub REST API。
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REPO = "wxj881027/QmClient"
DEFAULT_OUT_DIR = REPO_ROOT / ".github/assets"

# QmClient 贡献者邮箱。同一人存在多个 Git 身份,这里统一按邮箱归并。
QM_AUTHOR_EMAILS = (
	"1557555184@qq.com",
	"yuanliyinling@gmail.com",
	"2631560970@qq.com",
)

CHART_WIDTH = 840
CHART_HEIGHT = 280
PAD_LEFT = 56
PAD_RIGHT = 20
PAD_TOP = 28
PAD_BOTTOM = 38

LINE_COLOR = "#58a6ff"
LINE_FILL = "#58a6ff"
BAR_COLOR = "#3fb950"
TEXT_COLOR = "#8b949e"
GRID_COLOR = "#8b949e"

FONT_STACK = "-apple-system,BlinkMacSystemFont,Segoe UI,Helvetica,Arial,sans-serif"


def _svg_header(title: str) -> list[str]:
	return [
		f'<svg xmlns="http://www.w3.org/2000/svg" width="{CHART_WIDTH}" height="{CHART_HEIGHT}"',
		f' viewBox="0 0 {CHART_WIDTH} {CHART_HEIGHT}" role="img" aria-label="{title}">',
		f"<title>{title}</title>",
	]


def _plot_area() -> tuple[int, int, int, int]:
	return (
		PAD_LEFT,
		PAD_TOP,
		CHART_WIDTH - PAD_RIGHT,
		CHART_HEIGHT - PAD_BOTTOM,
	)


def _y_grid(max_value: int, label_suffix: str) -> tuple[list[str], float]:
	"""返回 Y 轴网格线与顶部刻度值。"""
	left, top, right, bottom = _plot_area()
	parts: list[str] = []
	steps = 4
	for index in range(steps + 1):
		value = max_value * index / steps
		y = bottom - (bottom - top) * index / steps
		parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{right}" y2="{y:.1f}" stroke="{GRID_COLOR}" stroke-opacity="0.18" stroke-width="1"/>')
		parts.append(f'<text x="{left - 10}" y="{y + 4:.1f}" fill="{TEXT_COLOR}" font-size="12" font-family="{FONT_STACK}" text-anchor="end">{int(round(value))}{label_suffix}</text>')
	return parts, float(bottom - top)


def _x_labels(labels: list[str], positions: list[float]) -> list[str]:
	left, _top, _right, bottom = _plot_area()
	parts: list[str] = []
	# 标签过密时按固定步长抽稀,保证可读性。
	step = max(1, len(labels) // 8)
	for index in range(0, len(labels), step):
		x = positions[index]
		parts.append(f'<text x="{x:.1f}" y="{bottom + 22}" fill="{TEXT_COLOR}" font-size="12" font-family="{FONT_STACK}" text-anchor="middle">{labels[index]}</text>')
	return parts


def render_star_chart(dates: list[datetime], repo: str) -> str:
	"""star 累计增长折线图。dates 为每次 star 的时间戳,按时间升序。"""
	left, top, right, bottom = _plot_area()
	parts = _svg_header(f"{repo} star history")
	parts.append(f'<text x="{left}" y="18" fill="{TEXT_COLOR}" font-size="13" font-family="{FONT_STACK}">Stars</text>')

	if len(dates) < 2:
		parts.append(f'<text x="{CHART_WIDTH / 2}" y="{CHART_HEIGHT / 2}" fill="{TEXT_COLOR}" font-size="14" font-family="{FONT_STACK}" text-anchor="middle">Not enough star data yet</text>')
		parts.append("</svg>")
		return "\n".join(parts)

	start = dates[0].timestamp()
	end = dates[-1].timestamp()
	span = max(end - start, 1.0)
	max_stars = len(dates)

	grid, plot_height = _y_grid(max_stars, "")
	parts.extend(grid)

	points: list[tuple[float, float]] = [(left, bottom)]
	for index, moment in enumerate(dates, start=1):
		x = left + (right - left) * (moment.timestamp() - start) / span
		y = bottom - plot_height * index / max_stars
		points.append((x, y))

	line = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
	area = f"{line} {right:.1f},{bottom:.1f} {left:.1f},{bottom:.1f}"
	parts.append(f'<polygon points="{area}" fill="{LINE_FILL}" fill-opacity="0.12"/>')
	parts.append(f'<polyline points="{line}" fill="none" stroke="{LINE_COLOR}" stroke-width="2" stroke-linejoin="round"/>')

	last_x, last_y = points[-1]
	parts.append(f'<circle cx="{last_x:.1f}" cy="{last_y:.1f}" r="3.5" fill="{LINE_COLOR}"/>')
	parts.append(f'<text x="{last_x:.1f}" y="{last_y - 10:.1f}" fill="{LINE_COLOR}" font-size="12" font-family="{FONT_STACK}" text-anchor="end">{max_stars}</text>')

	labels = [moment.strftime("%Y-%m") for moment in dates]
	positions = [x for x, _y in points[1:]]
	parts.extend(_x_labels(labels, positions))
	parts.append("</svg>")
	return "\n".join(parts)


def render_commit_chart(months: list[tuple[str, int]]) -> str:
	"""按月的提交量柱状图。"""
	left, top, right, bottom = _plot_area()
	parts = _svg_header("QmClient monthly commits")
	parts.append(f'<text x="{left}" y="18" fill="{TEXT_COLOR}" font-size="13" font-family="{FONT_STACK}">Commits per month</text>')

	if not months:
		parts.append("</svg>")
		return "\n".join(parts)

	max_commits = max(count for _label, count in months)
	grid, plot_height = _y_grid(max_commits, "")
	parts.extend(grid)

	slot = (right - left) / len(months)
	bar_width = max(2.0, slot * 0.62)
	centers: list[float] = []
	for index, (_label, count) in enumerate(months):
		height = plot_height * count / max_commits if max_commits else 0.0
		x = left + slot * index + (slot - bar_width) / 2
		y = bottom - height
		parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_width:.1f}" height="{max(height, 1.0):.1f}" rx="2" fill="{BAR_COLOR}" fill-opacity="0.85"/>')
		centers.append(left + slot * index + slot / 2)

	parts.extend(_x_labels([label for label, _count in months], centers))
	peak_index = max(range(len(months)), key=lambda i: months[i][1])
	peak_x = centers[peak_index]
	peak_y = bottom - plot_height * months[peak_index][1] / max_commits
	parts.append(f'<text x="{peak_x:.1f}" y="{peak_y - 8:.1f}" fill="{BAR_COLOR}" font-size="12" font-family="{FONT_STACK}" text-anchor="middle">{months[peak_index][1]}</text>')
	parts.append("</svg>")
	return "\n".join(parts)


def fetch_star_dates(repo: str, token: Optional[str]) -> list[datetime]:
	"""取回全部 star 的时间戳(GitHub 的 stargazers 端点支持 star+json 媒体类型)。"""

	def fetch_page(url: str, use_token: Optional[str]) -> list[dict]:
		request = urllib.request.Request(url)
		request.add_header("Accept", "application/vnd.github.star+json")
		request.add_header("User-Agent", "QmClient-readme-charts")
		if use_token:
			request.add_header("Authorization", f"Bearer {use_token}")
		with urllib.request.urlopen(request, timeout=30) as response:
			return json.loads(response.read().decode("utf-8"))

	dates: list[datetime] = []
	page = 1
	while True:
		url = f"https://api.github.com/repos/{repo}/stargazers?per_page=100&page={page}"
		try:
			payload = fetch_page(url, token)
		except urllib.error.HTTPError as error:
			if token and error.code in (401, 403):
				# token 无效或权限不足时降级为匿名访问:公开仓库的 star 列表无需认证。
				token = None
				try:
					payload = fetch_page(url, None)
				except urllib.error.HTTPError as retry_error:
					raise SystemExit(f"GitHub API 返回 {retry_error.code}:无法读取 star 数据") from retry_error
			else:
				raise SystemExit(f"GitHub API 返回 {error.code}:无法读取 star 数据") from error
		except urllib.error.URLError as error:
			raise SystemExit(f"无法连接 GitHub API:{error.reason}") from error

		if not payload:
			break
		for entry in payload:
			stamp = entry.get("starred_at")
			if stamp:
				dates.append(datetime.fromisoformat(stamp.replace("Z", "+00:00")).astimezone(timezone.utc))
		if len(payload) < 100:
			break
		page += 1

	dates.sort()
	return dates


def collect_commit_months(author_emails: tuple[str, ...]) -> list[tuple[str, int]]:
	"""按作者邮箱过滤后,统计每月提交量。"""
	command = ["git", "log", "--no-merges", "--format=%ad", "--date=format:%Y-%m", "HEAD"]
	for email in author_emails:
		command.append(f"--author={email}")
	result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, check=False)
	if result.returncode != 0:
		raise SystemExit(f"git log 失败:{result.stderr.strip()}")

	counter = Counter(line.strip() for line in result.stdout.splitlines() if line.strip())
	return sorted(counter.items())


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", default=DEFAULT_REPO, help="owner/name,默认取 QmClient 仓库")
	parser.add_argument("--out-dir", default=str(DEFAULT_OUT_DIR), help="SVG 输出目录")
	parser.add_argument("--token", default=os.environ.get("GITHUB_TOKEN"), help="GitHub token,默认读 GITHUB_TOKEN")
	args = parser.parse_args()

	out_dir = Path(args.out_dir)
	out_dir.mkdir(parents=True, exist_ok=True)

	star_path = out_dir / "star-history.svg"
	try:
		stars = fetch_star_dates(args.repo, args.token)
		star_path.write_text(render_star_chart(stars, args.repo), encoding="utf-8", newline="\n")
		print(f"stars={len(stars)} -> {star_path}")
	except SystemExit as error:
		# star 时间戳端点要求认证;拿不到时保留已有图表,不让提交活动图一起失败。
		print(f"warning: skipped star chart ({error})", file=sys.stderr)

	months = collect_commit_months(QM_AUTHOR_EMAILS)
	commit_path = out_dir / "commit-activity.svg"
	commit_path.write_text(render_commit_chart(months), encoding="utf-8", newline="\n")

	print(f"months={len(months)} commits={sum(count for _label, count in months)} -> {commit_path}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
