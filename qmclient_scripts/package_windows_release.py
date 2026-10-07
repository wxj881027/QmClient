"""CMake Windows 发布目标：汇总完整 7z、安装器和专用便携 ZIP。"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

from prepare_setup_source import prepare
from verify_windows_package import verify

REPO_ROOT = Path(__file__).resolve().parents[1]


def publish_artifacts(artifacts: list[Path], output: Path) -> None:
	# 所有输入先校验，避免半构建产物覆盖上一次发布目录。
	if len({path.name.casefold() for path in artifacts}) != len(artifacts):
		raise ValueError("duplicate release artifact names")
	for artifact in artifacts:
		if not artifact.is_file() or artifact.stat().st_size == 0:
			raise ValueError(f"missing or empty release artifact: {artifact}")
	output.mkdir(parents=True, exist_ok=True)
	checksums = []
	staged = []
	for artifact in artifacts:
		destination = output / artifact.name
		temporary = output / (artifact.name + ".publishing")
		shutil.copyfile(artifact, temporary)
		with temporary.open("rb") as source:
			digest = hashlib.file_digest(source, "sha256").hexdigest()
		staged.append((temporary, destination))
		checksums.append(f"{digest}  {artifact.name}")
	for temporary, destination in staged:
		temporary.replace(destination)
	temporary_sums = output / "SHA256SUMS.txt.publishing"
	temporary_sums.write_text("\n".join(checksums) + "\n", encoding="utf-8")
	temporary_sums.replace(output / "SHA256SUMS.txt")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build-dir", type=Path, required=True)
	parser.add_argument("--output-dir", type=Path, required=True)
	parser.add_argument("--package-name", required=True)
	parser.add_argument("--portable-package-name", required=True)
	parser.add_argument("--version", required=True)
	parser.add_argument("--jobs", type=int, default=6)
	args = parser.parse_args()
	if args.jobs < 1:
		parser.error("--jobs must be positive")
	build = args.build_dir.resolve(strict=True)
	portable = build / "portable-build"
	output = args.output_dir.resolve()
	cache = dict(line.split("=", 1) for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines() if "=" in line and not line.startswith(("#", "//")))
	if cache.get("CMAKE_HOME_DIRECTORY:INTERNAL", "").replace("\\", "/").casefold() != REPO_ROOT.as_posix().casefold():
		raise ValueError("release build belongs to another source directory")
	if cache.get("CMAKE_BUILD_TYPE:STRING") != "Release" or any(cache.get(name + ":BOOL") == "ON" for name in ("QMCLIENT_PORTABLE", "QMCLIENT_TEST_STORAGE")):
		raise ValueError("release target requires a normal Release build")
	if REPO_ROOT not in output.parents or output == build or output in build.parents or output == portable or portable in output.parents:
		raise ValueError("release output must be a separate workspace-local directory")
	compiler = shutil.which("iscc") or shutil.which("ISCC")
	if not compiler:
		raise ValueError("Inno Setup ISCC is required for package_windows_release")
	# 主目录的锁由外层 CMake 封装持有；这里仅构建独立便携子目录，避免递归申请主锁。
	wrapper = str(REPO_ROOT / "qmclient_scripts/cmake-windows.cmd")
	subprocess.run([wrapper, "-G", "Ninja", "-S", str(REPO_ROOT), "-B", str(portable), "-DCMAKE_BUILD_TYPE=Release", "-DQMCLIENT_PORTABLE=ON", "-DQMCLIENT_TEST_STORAGE=OFF", "-DMYSQL=OFF"], check=True, cwd=REPO_ROOT)
	subprocess.run([wrapper, "--build", str(portable), "--target", "package_zip", "-j", str(args.jobs)], check=True, cwd=REPO_ROOT)
	normal_package = build / (args.package_name + ".7z")
	portable_package = portable / (args.portable_package_name + ".zip")
	verify(normal_package, build / "qmclient-setup-runtime.txt", build / "qmclient-archive-tools.txt", False)
	verify(portable_package, portable / "qmclient-setup-runtime.txt", portable / "qmclient-archive-tools.txt", True)
	payload = build / "release/setup-source"
	installer_output = build / "release/windows-installer"
	installer_output.mkdir(parents=True, exist_ok=True)
	prepare(build, REPO_ROOT / "data", payload)
	result = subprocess.run([compiler, f"/DSourceDir={payload}", f"/DOutputDir={installer_output}", f"/DAppVersion={args.version}", str(REPO_ROOT / "qmclient_scripts/installer/QmClient.iss")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
	log = build / "release/windows-installer.log"
	log.write_bytes(result.stdout)
	if result.returncode or b"Warning:" in result.stdout:
		raise RuntimeError(f"Setup compilation failed or emitted warnings; see {log}")
	publish_artifacts([normal_package, installer_output / "QmClient-Setup.exe", portable_package], output)
	print(f"Windows release artifacts and SHA256SUMS.txt: {output}", flush=True)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
