"""在启动进程前准备独立的客户端测试构建，保留发布构建的存储模式。"""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
from collections.abc import Callable, Sequence

REPO_ROOT = Path(__file__).resolve().parents[2]


class ProcessBuildError(RuntimeError):
	pass


def read_cmake_cache(build_dir: Path) -> dict[str, str]:
	cache_path = build_dir / "CMakeCache.txt"
	if not cache_path.is_file():
		raise ProcessBuildError(f"missing CMakeCache.txt: {cache_path}; configure the requested build first")
	values: dict[str, str] = {}
	for line in cache_path.read_text(encoding="utf-8").splitlines():
		if line.startswith(("#", "//")) or "=" not in line:
			continue
		declaration, value = line.split("=", 1)
		key, separator, _ = declaration.partition(":")
		if separator:
			values[key] = value
	return values


def _enabled(cache: dict[str, str], key: str) -> bool:
	return cache.get(key, "").upper() in {"ON", "TRUE", "YES", "1"}


def _isolated(cache: dict[str, str]) -> bool:
	return _enabled(cache, "DEV") and _enabled(cache, "QMCLIENT_TEST_STORAGE") and not _enabled(cache, "QMCLIENT_PORTABLE")


def _source(cache: dict[str, str]) -> Path:
	value = cache.get("CMAKE_HOME_DIRECTORY")
	if not value:
		raise ProcessBuildError("CMakeCache.txt does not identify its source directory")
	return Path(value).resolve()


def _jobs(source: Path) -> int:
	profile = source / ".agents" / "machine.local.json"
	if not profile.is_file():
		return 3
	try:
		settings = json.loads(profile.read_text(encoding="utf-8-sig"))
		values = [settings[key] for key in ("build_jobs_per_slot_parallel", "build_jobs_single", "total_build_job_budget")]
		if any(type(value) is not int or value <= 0 for value in values):
			raise ValueError("build job counts must be positive integers")
		return min(values)
	except (OSError, ValueError, KeyError, TypeError) as error:
		raise ProcessBuildError(f"invalid build resource settings in {profile}: {error}") from error


def _run(command: Sequence[str], source: Path) -> None:
	try:
		subprocess.run(list(command), cwd=source, env={**os.environ, "PYTHONUTF8": "1"}, check=True)
	except (OSError, subprocess.CalledProcessError) as error:
		raise ProcessBuildError(f"process-test build failed: {error}") from error


def prepare_process_build(build_dir: Path, targets: tuple[str, ...] = ("game-client",), *, windows: bool | None = None, run_command: Callable[[Sequence[str], Path], None] | None = None) -> Path:
	requested = build_dir.resolve()
	if not (os.name == "nt" if windows is None else windows):
		return requested
	cache = read_cmake_cache(requested)
	source = _source(cache)
	if source != REPO_ROOT.resolve() or not requested.is_relative_to(source):
		raise ProcessBuildError(f"build directory belongs to another workspace: {requested}")
	jobs = _jobs(source)
	run = run_command or _run
	build_type = cache.get("CMAKE_BUILD_TYPE") or "Release"
	generator = cache.get("CMAKE_GENERATOR", "Ninja")
	selected = requested
	if not _isolated(cache):
		preferred = requested.with_name(requested.name + "-process-tests")
		candidates = [preferred, *sorted(requested.parent.glob("cmake-build-*"))]
		selected = preferred
		for candidate in candidates:
			if not (candidate / "CMakeCache.txt").is_file():
				continue
			candidate_cache = read_cmake_cache(candidate)
			if candidate == preferred and (_source(candidate_cache) != source or not _isolated(candidate_cache)):
				raise ProcessBuildError(f"dedicated process-test directory is already configured for another source or storage mode: {candidate}")
			if (
				_source(candidate_cache) == source
				and _isolated(candidate_cache)
				and all(candidate_cache.get(key, "").casefold() == cache.get(key, "").casefold() for key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"))
				and (candidate_cache.get("CMAKE_BUILD_TYPE") or "Release") == build_type
				and candidate_cache.get("CMAKE_GENERATOR", "Ninja") == generator
			):
				selected = candidate.resolve()
				break
		# 配置沿用待测构建的功能开关，只在独立缓存中启用隔离存储。
		configure = ["-G", generator, "-S", str(source), "-B", str(selected), f"-DCMAKE_BUILD_TYPE={build_type}"]
		for key in ("IPO", "VULKAN", "VIDEORECORDER", "PREFER_BUNDLED_LIBS", "PRECOMPILE_HEADERS", "DOWNLOAD_GTEST", "DOWNLOAD_BENCHMARK", "QM_MSVC_OPTIMIZE_GLOBAL_DATA", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"):
			if key in cache:
				configure.append(f"-D{key}={cache[key]}")
		configure.extend(["-DDEV=ON", "-DCLIENT=ON", "-DQMCLIENT_TEST_STORAGE=ON", "-DQMCLIENT_PORTABLE=OFF"])
		if "game-server" in targets:
			configure.append("-DSERVER=ON")
		print(f"[process-test] {requested} -> {selected}", flush=True)
		run(["cmd", "/c", str(source / "qmclient_scripts" / "cmake-windows.cmd"), *configure], source)
	if not selected.resolve().is_relative_to(source):
		raise ProcessBuildError(f"process-test build is outside this workspace: {selected}")
	if selected == requested and "game-server" in targets and not _enabled(cache, "SERVER"):
		run(["cmd", "/c", str(source / "qmclient_scripts" / "cmake-windows.cmd"), "-S", str(source), "-B", str(selected), "-DSERVER=ON"], source)
	selected_cache = read_cmake_cache(selected)
	if _source(selected_cache) != source or not _isolated(selected_cache):
		raise ProcessBuildError(f"process-test build is not isolated: {selected}")
	# 即使 exe 已存在也执行增量构建，不能把旧程序当作当前源码的验证结果。
	run(["cmd", "/c", str(source / "qmclient_scripts" / "cmake-windows.cmd"), "--build", str(selected), "--config", build_type, "--target", *targets, "-j", str(jobs)], source)
	built_cache = read_cmake_cache(selected)
	if _source(built_cache) != source or not _isolated(built_cache):
		raise ProcessBuildError(f"process-test storage mode changed during build: {selected}")
	for target, binary in (("game-client", "DDNet.exe"), ("game-server", "DDNet-Server.exe")):
		if target in targets and not (selected / binary).is_file():
			raise ProcessBuildError(f"build did not produce {selected / binary}")
	return selected
