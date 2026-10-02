"""串行构建、枚举、运行并校验 QmClient Google Benchmark。"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import uuid

if __package__:
	from .results import ResultError, summarize_results
else:
	from results import ResultError, summarize_results

REPO_ROOT = Path(__file__).resolve().parents[2]


class RunError(RuntimeError):
	pass


def write_json(path: Path, document: dict) -> None:
	path.write_text(json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def run_logged(command: list[str], cwd: Path, log: Path, timeout: float | None) -> str:
	try:
		result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, check=False)
	except subprocess.TimeoutExpired as error:
		stdout = error.stdout or b""
		stderr = error.stderr or b""
		if isinstance(stdout, bytes):
			stdout = stdout.decode("utf-8", errors="replace")
		if isinstance(stderr, bytes):
			stderr = stderr.decode("utf-8", errors="replace")
		log.write_text(stdout + stderr, encoding="utf-8")
		raise RunError(f"command timed out after {timeout}s; see {log}") from error
	log.write_text(result.stdout + result.stderr, encoding="utf-8")
	if result.returncode:
		raise RunError(f"command exited with {result.returncode}; see {log}")
	return result.stdout


def execute_benchmarks(command: list[str], cwd: Path, output: Path, *, pattern: str, min_time: float = 0.2, warmup: float = 0.05, repetitions: int = 5, interleave: bool = True, smoke: bool = False, timeout: float = 600) -> dict:
	common = [f"--benchmark_filter={pattern}"]
	listing = run_logged(command + common + ["--benchmark_list_tests=true"], cwd, output / "list.log", timeout)
	expected = [name.strip() for name in listing.splitlines() if name.strip()]
	if not expected:
		raise RunError("benchmark filter matched no cases; see list.log")
	if len(set(expected)) != len(expected):
		raise RunError("benchmark listing contains duplicate cases")
	options = common + [
		f"--benchmark_min_time={min_time}s",
		f"--benchmark_min_warmup_time={warmup}",
		f"--benchmark_repetitions={repetitions}",
		f"--benchmark_enable_random_interleaving={str(interleave).lower()}",
		f"--benchmark_dry_run={str(smoke).lower()}",
		"--benchmark_report_aggregates_only=false",
		"--benchmark_display_aggregates_only=true",
		"--benchmark_color=false",
		"--benchmark_format=console",
		"--benchmark_out_format=json",
		f"--benchmark_out={output / 'results.json'}",
	]
	write_json(output / "command.json", {"cwd": str(cwd), "command": command + options, "expected_cases": expected})
	print(f"[benchmark] {len(expected)} cases; {'smoke' if smoke else str(repetitions) + ' repetitions'}", flush=True)
	run_logged(command + options, cwd, output / "benchmark.log", timeout)
	raw = (output / "results.json").read_bytes()
	encoding = "utf-8"
	try:
		text = raw.decode(encoding)
	except UnicodeDecodeError:
		if os.name != "nt":
			raise
		# 上游 Windows 主机名使用 ANSI；记录回退，原始文件保持字节不变。
		encoding = "mbcs"
		text = raw.decode(encoding)
	document = json.loads(text)
	summary = summarize_results(document, expected, 1 if smoke else repetitions, smoke=smoke)
	summary["json_encoding"] = encoding
	summary["status"] = "SMOKE" if smoke else "MEASURED"
	if not smoke and summary["context"].get("library_build_type") != "release":
		raise RunError("Google Benchmark library was not built in release mode")
	return summary


def source_state(repo: Path) -> dict:
	def git(*arguments: str) -> bytes:
		result = subprocess.run(["git", *arguments], cwd=repo, capture_output=True, timeout=30, check=True)
		return result.stdout

	revision = git("rev-parse", "HEAD").decode().strip()
	digest = hashlib.sha256(git("diff", "--binary", "HEAD", "--", "src", "cmake", "CMakeLists.txt"))
	untracked = git("ls-files", "--others", "--exclude-standard", "-z", "--", "src", "cmake").split(b"\0")
	for name in sorted(name for name in untracked if name):
		digest.update(name + b"\0")
		digest.update((repo / os.fsdecode(name)).read_bytes())
	return {"revision": revision, "source_diff_sha256": digest.hexdigest()}


def read_build_settings(build: Path) -> dict[str, str]:
	settings = {}
	for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
		if line.startswith(("//", "#")) or ":" not in line or "=" not in line:
			continue
		key = line.split(":", 1)[0]
		if key.startswith(("CMAKE_CXX_", "CMAKE_HOME_DIRECTORY", "CMAKE_BUILD_TYPE", "CMAKE_GENERATOR", "CMAKE_MSVC_RUNTIME", "CMAKE_INTERPROCEDURAL", "BENCHMARK_", "DOWNLOAD_BENCHMARK")):
			settings[key] = line.split("=", 1)[1]
	return settings


def build_command(repo: Path, build: Path, jobs: int) -> list[str]:
	arguments = ["--build", str(build), "--target", "qm-benchmarks", "-j", str(jobs)]
	if os.name == "nt":
		return ["cmd.exe", "/d", "/c", str(Path("qmclient_scripts") / "cmake-windows.cmd"), *arguments]
	return ["cmake", *arguments]


def _finite(value: str) -> float:
	number = float(value)
	if not math.isfinite(number) or number < 0:
		raise argparse.ArgumentTypeError("expected a finite nonnegative number")
	return number


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build-dir", type=Path, default=Path("cmake-build-release"))
	parser.add_argument("--filter", required=True, help="Google Benchmark 正则；全量用 .*")
	parser.add_argument("--output-dir", type=Path)
	parser.add_argument("--min-time", type=_finite, default=0.2)
	parser.add_argument("--warmup", type=_finite, default=0.05)
	parser.add_argument("--repetitions", type=int, default=5)
	parser.add_argument("--interleave", action=argparse.BooleanOptionalAction, default=True)
	parser.add_argument("--smoke", action="store_true", help="仅检查所有 case 能运行，不能作为性能证据")
	parser.add_argument("--timeout", type=_finite, default=600, help="枚举及测量进程超时秒数；不取消构建的子进程")
	parser.add_argument("-j", "--jobs", type=int, default=14)
	args = parser.parse_args(argv)
	if args.min_time <= 0 or args.timeout <= 0 or args.repetitions < 1 or args.jobs < 1:
		parser.error("min-time, timeout, repetitions and jobs must be positive")
	build = (REPO_ROOT / args.build_dir).resolve()
	output = (REPO_ROOT / (args.output_dir or Path("tmp/benchmarks") / f"run-{uuid.uuid4().hex[:12]}")).resolve()
	metadata: dict = {"status": "RUNNING", "build_dir": str(build), "output_dir": str(output), "smoke": args.smoke}
	try:
		# 独立目录保留原始结果；不覆盖已有基线或读取遗留 JSON 充当本次输出。
		output.mkdir(parents=True, exist_ok=False)
	except OSError as error:
		print(f"[benchmark] cannot reserve output directory: {error}", file=sys.stderr)
		return 1
	try:
		metadata["source_before"] = source_state(REPO_ROOT)
		metadata["build_settings"] = read_build_settings(build)
		if not args.smoke and metadata["build_settings"].get("CMAKE_BUILD_TYPE") not in {"Release", "RelWithDebInfo"}:
			raise RunError("measurement requires a Release or RelWithDebInfo build")
		if Path(metadata["build_settings"].get("CMAKE_HOME_DIRECTORY", "")).resolve() != REPO_ROOT:
			raise RunError("build directory belongs to a different source checkout")
		write_json(output / "run.json", metadata)
		print(f"[build] qm-benchmarks; logs: {output}", flush=True)
		run_logged(build_command(REPO_ROOT, build, args.jobs), REPO_ROOT, output / "build.log", None)
		metadata["build_settings"] = read_build_settings(build)
		executable = build / ("qm-benchmarks.exe" if os.name == "nt" else "qm-benchmarks")
		metadata["binary_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
		summary = execute_benchmarks([str(executable)], build, output, pattern=args.filter, min_time=args.min_time, warmup=args.warmup, repetitions=args.repetitions, interleave=args.interleave, smoke=args.smoke, timeout=args.timeout)
		metadata["source_after"] = source_state(REPO_ROOT)
		if metadata["source_before"] != metadata["source_after"]:
			raise RunError("production or benchmark source changed during build/measurement; results are not a stable baseline")
		if metadata["binary_sha256"] != hashlib.sha256(executable.read_bytes()).hexdigest():
			raise RunError("benchmark executable changed during measurement")
		write_json(output / "summary.json", summary)
		metadata["status"] = summary["status"]
		print(f"[{metadata['status']}] {len(summary['cases'])} cases; {output / 'summary.json'}")
		return 0
	except (OSError, ValueError, ResultError, RunError, subprocess.SubprocessError) as error:
		metadata["status"] = "FAILED"
		metadata["error"] = str(error)
		print(f"[benchmark] {error}", file=sys.stderr)
		return 1
	finally:
		write_json(output / "run.json", metadata)


if __name__ == "__main__":
	raise SystemExit(main())
