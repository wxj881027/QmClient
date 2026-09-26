# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""QmClient 仓库级门禁总入口（Python 版）。

纯编排层：只负责模式定义、参数解析、范围收集和检查调度。
具体检查逻辑下沉到 checks/ 各模块中。
"""

from __future__ import annotations

import argparse
import sys
import traceback
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Literal

# 确保 gate/lib 和 gate/checks 在路径上
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from checks import (  # noqa: E402
	clang_format,
	clang_tidy_warn,
	ci_build,
	config_vars,
	dilate,
	env,
	headers,
	identifiers,
	python,
	python_tests,
	qm_smoke,
	shell,
	settings_ui,
	strict_build,
	style,
	tests,
)
from lib import runner, scope  # noqa: E402
from lib.report import ResultCollector  # noqa: E402

REPO_ROOT = SCRIPT_DIR.parents[1]

# ------------------------------------------------------------------
# 模式定义
# ------------------------------------------------------------------

_MODE_SPECS: dict[str, dict] = {
	"quick": {
		"target": "开发期快速自查",
		"expectation": "通常应在数分钟内完成，只扫源码卫生层。",
		"blocking_rule": "只阻断明显的脚本/规范问题，不做真实构建与测试。",
		"tests": {"cxx": False, "rust": False, "all": False},
	},
	"default": {
		"target": "日常提交前严格门",
		"expectation": "运行 quick 层源码卫生检查，并覆盖 C++ 全量测试和 Rust 全量测试。",
		"blocking_rule": "quick 层检查或全量测试任一失败都应阻断。",
		"tests": {"cxx": True, "rust": True, "all": False},
	},
	"full": {
		"target": "集中收口 / 准发布门",
		"expectation": "在 default 基础上增加更重的附加检查。",
		"blocking_rule": "默认阻断 default 层和 full 的硬失败项；高噪音附加检查先以 WARN 方式试跑。",
		"tests": {"cxx": True, "rust": True, "all": False},
	},
	"build": {
		"target": "CI 等价构建验证",
		"expectation": "在本地复现 CI 的 clang-tidy 和 sanitizer 流程。耗时较长，仅在需要验证 CI 行为时执行。",
		"blocking_rule": "任一构建失败即阻断。",
		"tests": {"cxx": False, "rust": False, "all": False},
	},
}


@dataclass(frozen=True)
class CheckSpec:
	name: str
	module: ModuleType
	default_modes: frozenset[str]
	skip_attr: str | None = None
	enable_attr: str | None = None
	enable_modes: frozenset[str] = frozenset()
	scope_kind: Literal["cpp", "changed"] = "cpp"
	needs_base_ref: bool = False

	def selected(self, mode: str, args: argparse.Namespace) -> bool:
		return mode in self.default_modes or (mode in self.enable_modes and self.enable_attr is not None and bool(getattr(args, self.enable_attr)))

	def skip_reason(self, args: argparse.Namespace) -> str:
		if self.skip_attr is None or not bool(getattr(args, self.skip_attr)):
			return ""
		return "--" + self.skip_attr.replace("_", "-")


@dataclass(frozen=True)
class GateContext:
	args: argparse.Namespace
	scope_result: scope.ScopeResult


_SOURCE_MODES = frozenset({"quick", "default", "full"})
_ALL_MODES = frozenset({"quick", "default", "full", "build"})
_CHECK_SPECS = (
	CheckSpec("env", env, _ALL_MODES, skip_attr="skip_preflight"),
	CheckSpec("config_vars", config_vars, _ALL_MODES, skip_attr="skip_config_checks"),
	CheckSpec("headers", headers, _ALL_MODES, skip_attr="skip_header_checks"),
	CheckSpec("style", style, _ALL_MODES, skip_attr="skip_style_check"),
	CheckSpec("python", python, _ALL_MODES, skip_attr="skip_ruff_check"),
	CheckSpec("python_tests", python_tests, frozenset({"default", "full"}), skip_attr="skip_python_tests"),
	CheckSpec("qm_smoke", qm_smoke, frozenset(), enable_attr="run_qm_smoke", enable_modes=frozenset({"default", "full"}), scope_kind="changed"),
	CheckSpec("shell", shell, _ALL_MODES, skip_attr="skip_shell_check"),
	CheckSpec("settings_ui", settings_ui, _SOURCE_MODES, scope_kind="changed"),
	CheckSpec("strict_build", strict_build, frozenset({"full"}), skip_attr="skip_strict_debug", needs_base_ref=True),
	CheckSpec("dilate", dilate, frozenset({"full"}), skip_attr="skip_dilate_check"),
	CheckSpec(
		"identifiers",
		identifiers,
		frozenset({"full"}),
		enable_attr="include_identifier_check",
		enable_modes=_SOURCE_MODES,
		needs_base_ref=True,
	),
	CheckSpec(
		"clang_format",
		clang_format,
		frozenset(),
		enable_attr="enable_clang_format_check",
		enable_modes=_SOURCE_MODES,
	),
	CheckSpec(
		"clang_tidy_warn",
		clang_tidy_warn,
		frozenset({"full"}),
		enable_attr="enable_full_clang_tidy_warn",
		enable_modes=_SOURCE_MODES,
	),
	CheckSpec("ci_build", ci_build, frozenset({"build"}), skip_attr="skip_ci_build"),
)


# ------------------------------------------------------------------
# 参数解析
# ------------------------------------------------------------------


def _parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="QmClient 仓库级门禁总入口")
	parser.add_argument("--build-dir", default="cmake-build-release")
	parser.add_argument(
		"--base-ref",
		default=None,
		help="差异基线；默认使用 origin/HEAD，缺失时依次回退 origin/master、origin/main、master、main",
	)
	parser.add_argument("--mode", choices=["quick", "default", "full", "build"], default="default")
	parser.add_argument("--skip-ci-build", action="store_true")
	parser.add_argument("--skip-preflight", action="store_true")
	parser.add_argument("--skip-config-checks", action="store_true")
	parser.add_argument("--skip-header-checks", action="store_true")
	parser.add_argument("--skip-style-check", action="store_true")
	parser.add_argument("--skip-strict-debug", action="store_true")
	parser.add_argument("--skip-cxx-tests", action="store_true")
	parser.add_argument("--run-all-tests", action="store_true")
	parser.add_argument("--include-identifier-check", action="store_true")
	parser.add_argument("--enable-clang-format-check", action="store_true")
	parser.add_argument("--enable-full-clang-tidy-warn", action="store_true")
	parser.add_argument("--skip-ruff-check", action="store_true")
	parser.add_argument("--skip-python-tests", action="store_true")
	parser.add_argument("--run-qm-smoke", action="store_true")
	parser.add_argument("--skip-shell-check", action="store_true")
	parser.add_argument("--skip-dilate-check", action="store_true")
	parser.add_argument(
		"--ci-mode",
		action="store_true",
		help="兼容现有 CI 调用；当前与本地检查集合一致",
	)
	parser.add_argument("--dry-run", action="store_true")
	parser.add_argument("--explain-scope", action="store_true")
	parser.add_argument("--branch-scope-only", action="store_true")
	parser.add_argument("--report-json-path")
	parser.add_argument("--scope-report-path")
	return parser.parse_args()


# ------------------------------------------------------------------
# 调度逻辑
# ------------------------------------------------------------------


def _run_checks(context: GateContext, results: ResultCollector) -> None:
	args = context.args
	scope_result = context.scope_result
	for spec in _CHECK_SPECS:
		if not spec.selected(args.mode, args):
			continue
		skip_reason = spec.skip_reason(args)
		if skip_reason:
			results.add_skip(f"Gate 检查: {spec.name}", f"显式跳过: {skip_reason}")
			continue
		files = scope_result.changed if spec.scope_kind == "changed" else scope_result.included
		if spec.needs_base_ref:
			spec.module.run(results, files, args.dry_run, base_ref=args.base_ref)
		else:
			if spec.name == "qm_smoke":
				spec.module.run(results, files, args.dry_run, build_dir=args.build_dir)
			else:
				spec.module.run(results, files, args.dry_run)


def _run_tests(context: GateContext, results: ResultCollector) -> None:
	args = context.args
	test_spec = _MODE_SPECS[args.mode].get("tests", {})
	run_cxx = test_spec.get("cxx", False) and not args.skip_cxx_tests and not args.run_all_tests
	run_rust = test_spec.get("rust", False) and not args.run_all_tests
	if test_spec.get("cxx", False) and args.skip_cxx_tests and not args.run_all_tests:
		results.add_skip("C++ 全量测试", "显式跳过: --skip-cxx-tests")
	if run_cxx or run_rust or args.run_all_tests:
		tests.run(
			results,
			context.scope_result.included,
			dry_run=args.dry_run,
			build_dir=args.build_dir,
			run_all=args.run_all_tests,
			run_cxx=run_cxx,
			run_rust=run_rust,
		)


def _finalize_run(
	args: argparse.Namespace,
	mode_spec: dict,
	results: ResultCollector,
	scoped_files: list[str],
	changed_files: list[str],
) -> None:
	report_path = Path(args.report_json_path) if args.report_json_path else None
	try:
		results.write_json(
			report_path,
			mode=args.mode,
			mode_spec={
				"Name": args.mode,
				"Target": mode_spec["target"],
				"Expectation": mode_spec["expectation"],
				"BlockingRule": mode_spec["blocking_rule"],
			},
			scoped_files=scoped_files,
			changed_files=changed_files,
			dry_run=args.dry_run,
		)
	except Exception:
		results.add("FAIL", "JSON 报告写入", traceback.format_exc())
	results.write_summary(
		mode=args.mode,
		mode_target=mode_spec["target"],
		mode_expectation=mode_spec["expectation"],
		mode_blocking_rule=mode_spec["blocking_rule"],
		dry_run=args.dry_run,
	)


def main() -> int:
	args = _parse_args()
	if args.base_ref is None:
		args.base_ref = scope.default_base_ref()

	if args.mode not in _MODE_SPECS:
		print(f"未知 mode: {args.mode}", file=sys.stderr)
		return 2
	mode_spec = _MODE_SPECS[args.mode]

	allowlist_path = REPO_ROOT / "qmclient_scripts" / "gate" / "baseline_debt_allowlist.json"
	results = ResultCollector(allowlist_path if allowlist_path.exists() else None)
	scoped_files: list[str] = []
	changed_files: list[str] = []
	finalize_failed = False

	try:
		# 范围收集
		sc = scope.collect_scope(args.base_ref, args.branch_scope_only)
		scoped_files = sc.included
		changed_files = sc.changed
		results.add(
			"INFO",
			"差异范围统计",
			f"branch={len(sc.branch)}, unstaged={len(sc.unstaged)}, staged={len(sc.staged)}, untracked={len(sc.untracked)}, included={len(sc.included)}, excluded={len(sc.excluded)}",
		)
		if not sc.base_ref_available:
			msg = f"差异基线不可用: {args.base_ref}"
			if sc.base_ref_failure_reason:
				msg += f" ({sc.base_ref_failure_reason})"
			results.add("WARN", "差异基线检查", msg)

		if args.explain_scope:
			runner.print_section("差异范围说明")
			print(f"BaseRef: {args.base_ref}")
			print(f"BaseRef 可用: {sc.base_ref_available}")
			if sc.base_ref_failure_reason:
				print(f"BaseRef 失败原因: {sc.base_ref_failure_reason}")
			print(f"纳入首方范围文件数: {len(sc.included)}")
			print(f"排除文件数: {len(sc.excluded)}")

		scope_report_path = Path(args.scope_report_path) if args.scope_report_path else None
		results.write_scope_json(
			scope_report_path,
			args.base_ref,
			sc.base_ref_available,
			sc.base_ref_failure_reason,
			sc.included,
			sc.excluded,
			sc.changed,
		)

		context = GateContext(args=args, scope_result=sc)
		_run_checks(context, results)
		_run_tests(context, results)
	except KeyboardInterrupt:
		results.add("FAIL", "Gate 入口中断", "收到 KeyboardInterrupt，已提前终止。")
	except Exception:
		results.add("FAIL", "Gate 入口异常", traceback.format_exc())
	finally:
		try:
			_finalize_run(args, mode_spec, results, scoped_files, changed_files)
		except Exception:
			finalize_failed = True
			print("\n[FAIL] Gate 收尾异常", file=sys.stderr)
			print(traceback.format_exc(), file=sys.stderr)

	print("\n仓库级检查完成。")
	return 1 if results.has_failures() or finalize_failed else 0


if __name__ == "__main__":
	sys.exit(main())
