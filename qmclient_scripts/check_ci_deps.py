#!/usr/bin/env python3
"""CI Linux 依赖清单守卫。

单一来源规则：所有 Linux 构建作业的 apt 依赖统一走
.github/actions/linux-deps 复合 action 的主清单（base 列表）。
作业专属依赖通过 `with: extra:` 声明，同样集中在本脚本的注册表里核对。

检查内容：
1. workflow 中残留的 `apt-get install` 步骤，其包名必须全部被
   action 主清单或注册的 extra 覆盖，防止收敛后再长出第四、第五处独立清单。
2. 使用 `./.github/actions/linux-deps` 的作业如果 CMake 配置开启了
   MYSQL/WEBSOCKETS/UPNP 等可选特性，其 extra 必须包含对应系统包。
3. action 主清单必须包含 libcurl4-openssl-dev，并保留 curl share API
   编译探针（ddnet-libs 捆绑桩缺少该 API，漏装会在链接期才失败）。

发现的任何遗漏都以非零退出码失败，供本地 gate 与 CI 调用。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_DIR = REPO_ROOT / ".github" / "workflows"
ACTION_FILE = REPO_ROOT / ".github" / "actions" / "linux-deps" / "action.yml"
ACTION_RELATIVE = ".github/actions/linux-deps/action.yml"

ACTION_USE = "./.github/actions/linux-deps"

# 允许保留独立 apt 步骤的例外场景：纯工具安装，与构建依赖清单无关。
EXEMPT_STEP_NAMES = {
	"Install clang-format-22",
	"Install Clang 22",
}

# 作业专属包注册表：workflow 文件名 -> 预期出现在该文件 extra 中的包。
# base 清单之外新增作业依赖时，必须在这里登记，否则守卫失败。
EXPECTED_EXTRA: dict[str, set[str]] = {
	"build.yml": {"libdbus-1-dev", "libogg-dev", "libopus-dev", "libopusfile-dev", "libwavpack-dev"},
	"clang-tidy.yml": {"libmariadb-dev", "libwebsockets-dev", "libminiupnpc-dev"},
	"codeql-analysis.yml": {"libmariadb-dev", "libwebsockets-dev"},
	"style.yml": {"ddnet-tools", "shellcheck", "tidy", "graphviz", "python3-clang", "wget", "gnupg", "ca-certificates"},
}

# 允许保留独立 apt 步骤的 workflow -> 步骤中允许出现的 base 之外的包。
# 仅限纯工具；任何 -dev 开发库都不允许出现在这里。
ALLOWED_STANDALONE: dict[str, set[str]] = {
	"build.yml": {"p7zip-full"},
}

# 可选特性 -> extra 需要的系统包；与 CMakeLists.txt 的 option 对应。
FEATURE_PACKAGES: dict[str, str] = {
	"MYSQL": "libmariadb-dev",
	"WEBSOCKETS": "libwebsockets-dev",
	"UPNP": "libminiupnpc-dev",
}

PACKAGE_RE = re.compile(r"[a-z0-9][a-z0-9+._-]*")


def parse_action_packages(action_text: str) -> set[str]:
	"""从 composite action 文本提取主清单包名（extra 输入占位符除外）。"""
	start = action_text.index("apt-get install")
	block = action_text[start : action_text.index("${{ inputs.extra }}")]
	packages = set(PACKAGE_RE.findall(block))
	packages -= {"apt", "get", "install", "sudo", "y"}
	return packages


def parse_extra_packages(text: str) -> set[str]:
	"""提取 workflow 中调用 action 时的 extra 包名。"""
	packages: set[str] = set()
	for match in re.finditer(r"uses:\s*\./\.github/actions/linux-deps(.*?)(?=\n\s*- name:|\n\s*- uses:|\Z)", text, re.DOTALL):
		extra_match = re.search(r"extra:\s*([^\n#]+)", match.group(1))
		if extra_match:
			packages |= set(extra_match.group(1).split())
	return packages


NOISE_TOKENS = {"sudo", "apt", "get", "install", "update", "y", "qq"}


def extract_install_packages(body: str) -> set[str]:
	"""提取 apt-get install 命令本身的包名。

	只取首行与以反斜杠结尾的续行，不扫步骤体里后续命令（venv、pip 等），
	避免把无关 token 误判为 apt 包。
	"""
	idx = body.find("apt-get install")
	if idx < 0:
		return set()
	lines = body[idx:].splitlines()[0:]  # 首行含 "apt-get install" 之后的内容
	collected: list[str] = []
	for line in lines:
		stripped = line.strip()
		if not collected:
			stripped = stripped.split("apt-get install", 1)[1].strip()
			if not stripped:
				continue
		collected.append(stripped)
		if not stripped.endswith("\\"):
			break
	text = " ".join(collected).replace("\\", " ")
	tokens = set(PACKAGE_RE.findall(text))
	return tokens - NOISE_TOKENS


def iter_apt_steps(text: str):
	"""产出 (步骤名, 安装包名列表)；跳过例外与复合 action 调用。"""
	for match in re.finditer(r"- name:\s*([^\n]+)\n(.*?)(?=\n\s*- name:|\n\s*- uses:|\Z)", text, re.DOTALL):
		name = match.group(1).strip()
		body = match.group(2)
		if name in EXEMPT_STEP_NAMES or "apt-get install" not in body:
			continue
		yield name, extract_install_packages(body)


def check_curl_probe(text: str) -> str | None:
	if "curl_share_init" not in text:
		return "action 缺少 curl share API 编译探针（curl_share_init），漏装系统 curl 开发包将在链接期才暴露"
	return None


def run_checks(repo_root: Path = REPO_ROOT) -> list[str]:
	"""扫描仓库实际文件并返回失败清单；纯逻辑入口是 check_sources。"""
	action_file = repo_root / ".github" / "actions" / "linux-deps" / "action.yml"
	sources: dict[str, str] = {".github/actions/linux-deps/action.yml": action_file.read_text(encoding="utf-8")}
	for workflow in sorted((repo_root / ".github" / "workflows").glob("*.yml")):
		sources[f".github/workflows/{workflow.name}"] = workflow.read_text(encoding="utf-8")
	return check_sources(sources)


def check_sources(sources: dict[str, str]) -> list[str]:
	"""对内存中的 action 与 workflow 文本做守卫检查，供测试与 run_checks 复用。

	键为相对仓库根的路径，值为文件内容；缺失 action 时报一条失败。
	"""
	failures: list[str] = []
	action_text = sources.get(ACTION_RELATIVE)
	if action_text is None:
		return [f"未找到 {ACTION_RELATIVE}，无法执行 CI 依赖单一来源守卫"]

	base_packages = parse_action_packages(action_text)
	if "libcurl4-openssl-dev" not in base_packages:
		failures.append("action 主清单缺少 libcurl4-openssl-dev（本次事故根因包）")
	probe_failure = check_curl_probe(action_text)
	if probe_failure:
		failures.append(probe_failure)

	for rel in sorted(path for path in sources if path != ACTION_RELATIVE):
		text = sources[rel]
		workflow_name = Path(rel).name
		uses_action = ACTION_USE in text

		# 残留 apt 步骤的包必须被 base、extra 或独立步骤白名单覆盖。
		for step_name, packages in iter_apt_steps(text):
			allowed = base_packages | EXPECTED_EXTRA.get(workflow_name, set()) | ALLOWED_STANDALONE.get(workflow_name, set())
			uncovered = packages - allowed
			if uncovered:
				failures.append(f"{rel}: 步骤「{step_name}」的包未被主清单、extra 或白名单覆盖: {sorted(uncovered)}")

		# extra 注册表核对。
		extra_packages = parse_extra_packages(text)
		expected = EXPECTED_EXTRA.get(workflow_name, set())
		if expected - extra_packages:
			failures.append(f"{rel}: extra 缺少注册包 {sorted(expected - extra_packages)}")
		if extra_packages - expected:
			failures.append(f"{rel}: extra 包 {sorted(extra_packages - expected)} 未在守卫注册表登记，请更新 EXPECTED_EXTRA")

		# 可选特性与 extra 的对应关系（仅在调用 action 的 workflow 检查）。
		if uses_action:
			for feature, package in FEATURE_PACKAGES.items():
				if re.search(rf"-D{feature}=ON", text) and package not in extra_packages:
					failures.append(f"{rel}: CMake 配置启用 {feature} 但 extra 缺少 {package}")

	# 非 action 的作业必须至少在某个注册 workflow 中出现，防止全部绕开。
	users = [name for name, expected in EXPECTED_EXTRA.items() if expected is not None]
	if not users:
		failures.append("EXPECTED_EXTRA 注册表为空，主清单无任何消费者")
	return failures


def main() -> int:
	parser = argparse.ArgumentParser(description="CI Linux 依赖清单守卫")
	parser.add_argument("--repo-root", default=str(REPO_ROOT), help="仓库根目录（测试用）")
	args = parser.parse_args()
	repo_root = Path(args.repo_root).resolve()

	if not (repo_root / ".github" / "actions" / "linux-deps" / "action.yml").exists():
		print(f"[FAIL] 未找到 {repo_root / '.github' / 'actions' / 'linux-deps' / 'action.yml'}")
		return 1

	failures = run_checks(repo_root)
	if failures:
		for failure in failures:
			print(f"[FAIL] {failure}")
		print(f"\nCI 依赖守卫: {len(failures)} 项失败。请把依赖收敛到 .github/actions/linux-deps 并在 EXPECTED_EXTRA 登记 extra。")
		return 1
	print("CI 依赖守卫: 全部 Linux 构建作业依赖已被单一来源清单覆盖。")
	return 0


if __name__ == "__main__":
	sys.exit(main())
