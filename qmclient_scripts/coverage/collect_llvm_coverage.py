#!/usr/bin/env python3
"""Build and collect LLVM coverage for the C++ test runner."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(command: list[str], cwd: Path) -> None:
	print("+", " ".join(command))
	try:
		subprocess.run(command, cwd=cwd, check=True)
	except subprocess.CalledProcessError as error:
		if command and command[0].lower().endswith("cmake") and "-S" in command and "--build" not in command:
			raise RuntimeError(
				"coverage CMake 配置失败；请使用带 libatomic 的 Linux/CI Clang 工具链，"
				"不要把普通 MSVC/Release 构建当作 LLVM coverage。"
			) from error
		raise


def resolve_tool(name: str, override: str | None) -> str:
	tool = override or shutil.which(name)
	if not tool:
		raise RuntimeError(f"missing required tool: {name}")
	return tool


def resolve_testrunner(build_dir: Path, explicit: Path | None) -> Path:
	"""Resolve a runner for the current host without crossing binary formats."""
	if explicit is not None:
		candidate = explicit.resolve()
		candidates = [candidate]
		if os.name == "nt" and candidate.suffix == "":
			candidates.insert(0, candidate.with_suffix(".exe"))
	else:
		candidates = [build_dir / "testrunner.exe", build_dir / "testrunner"] if os.name == "nt" else [build_dir / "testrunner"]
	for candidate in candidates:
		if candidate.is_file():
			if os.name == "nt" and candidate.suffix.lower() != ".exe":
				raise RuntimeError(
					f"incompatible test runner for Windows: {candidate}; "
					"a Linux/WSL runner cannot be launched by native Python"
				)
			return candidate
	requested = explicit if explicit is not None else candidates[0]
	raise RuntimeError(f"missing test runner: {requested}; use --testrunner to select a compatible LLVM-instrumented binary")


def validate_build_dir(build_dir: Path) -> None:
	if build_dir.name.lower() in {"cmake-build-release", "cmake-build-release-msvc"}:
		raise RuntimeError("coverage requires a dedicated build directory; do not reuse a normal Release/MSVC build")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-dir", type=Path, default=Path("."))
	parser.add_argument("--build-dir", type=Path, default=Path("cmake-build-coverage"))
	parser.add_argument("--output-dir", type=Path, default=Path("tmp/coverage"))
	parser.add_argument("--filter", default="*")
	parser.add_argument("--compiler", default=None)
	parser.add_argument("--cxx-compiler", default=None)
	parser.add_argument("--llvm-profdata", default=None)
	parser.add_argument("--llvm-cov", default=None)
	parser.add_argument("--testrunner", type=Path, default=None, help="可选的已构建 testrunner 路径")
	parser.add_argument("--skip-build", action="store_true")
	args = parser.parse_args()

	source_dir = args.source_dir.resolve()
	build_dir = args.build_dir.resolve()
	validate_build_dir(build_dir)
	output_dir = args.output_dir.resolve()
	output_dir.mkdir(parents=True, exist_ok=True)

	clang = resolve_tool("clang", args.compiler)
	clangxx = resolve_tool("clang++", args.cxx_compiler)
	llvm_profdata = resolve_tool("llvm-profdata", args.llvm_profdata)
	llvm_cov = resolve_tool("llvm-cov", args.llvm_cov)

	if not args.skip_build:
		build_dir.mkdir(parents=True, exist_ok=True)
		run(
			[
				"cmake",
				"-S",
				str(source_dir),
				"-B",
				str(build_dir),
				"-G",
				"Ninja",
				"-DCMAKE_BUILD_TYPE=Debug",
				f"-DCMAKE_C_COMPILER={clang}",
				f"-DCMAKE_CXX_COMPILER={clangxx}",
				"-DDOWNLOAD_GTEST=ON",
				"-DHEADLESS_CLIENT=ON",
				"-DVULKAN=OFF",
				"-DDEV=ON",
				"-DCMAKE_C_FLAGS=-fprofile-instr-generate -fcoverage-mapping",
				"-DCMAKE_CXX_FLAGS=-fprofile-instr-generate -fcoverage-mapping",
			],
			source_dir,
		)
		run(["cmake", "--build", str(build_dir), "--target", "testrunner", "-j", "4"], source_dir)

	testrunner = resolve_testrunner(build_dir, args.testrunner)
	profraw = output_dir / "testrunner.profraw"
	env = os.environ.copy()
	env["LLVM_PROFILE_FILE"] = str(profraw)
	print("+", str(testrunner), f"--gtest_filter={args.filter}")
	subprocess.run([str(testrunner), f"--gtest_filter={args.filter}"], cwd=build_dir, env=env, check=True)

	profdata = output_dir / "testrunner.profdata"
	run([llvm_profdata, "merge", "-sparse", str(profraw), "-o", str(profdata)], source_dir)

	binary = str(testrunner)
	export_path = output_dir / "coverage.json"
	print("+", llvm_cov, "export", binary, f"-instr-profile={profdata}")
	with export_path.open("w", encoding="utf-8", newline="\n") as output:
		subprocess.run(
			[
				llvm_cov,
				"export",
				binary,
				f"-instr-profile={profdata}",
				"-show-branches=count",
			],
			cwd=source_dir,
			stdout=output,
			check=True,
		)
	text_report = output_dir / "coverage.txt"
	with text_report.open("w", encoding="utf-8") as output:
		subprocess.run(
			[
				llvm_cov,
				"report",
				binary,
				f"-instr-profile={profdata}",
				"-show-branch-summary",
				"-show-function-summary",
				"-show-region-summary",
			],
			cwd=source_dir,
			stdout=output,
			check=True,
		)
	print(f"coverage reports: {export_path}, {text_report}")
	return 0


if __name__ == "__main__":
	try:
		raise SystemExit(main())
	except (RuntimeError, subprocess.CalledProcessError) as error:
		print(f"coverage collection failed: {error}", file=sys.stderr)
		raise SystemExit(1) from error
