"""准备固定源码的 Windows librime 核心；默认离线，只有 --build 才编译。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request


REPO_ROOT = Path(__file__).resolve().parents[2]
LOCK_FILE = Path(__file__).with_name("rime_core_sources.json")
DEFAULT_WORK = REPO_ROOT / "tmp/ime/rime-core"


def file_sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def acquire(path: Path, url: str, expected: str, download: bool) -> Path:
    if not path.is_file():
        if not download:
            raise ValueError(f"离线输入缺失：{path}；先使用 --download 准备固定输入")
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = path.with_name(path.name + ".part")
        with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        if file_sha256(partial) != expected:
            raise ValueError(f"下载 SHA-256 不符：{url}；未接受该输入")
        partial.replace(path)
    if file_sha256(path) != expected:
        raise ValueError(f"固定输入 SHA-256 不符：{path}")
    return path


def unpack(archive: Path, destination: Path) -> Path:
    # filter=data 拒绝越界路径及特殊文件；缓存目录只承载本任务的固定源码。
    if sys.version_info < (3, 12):
        raise ValueError("安全解压需要 Python 3.12 或更新版本")
    with tarfile.open(archive) as source:
        members = source.getmembers()
        roots = {member.name.split("/", 1)[0] for member in members}
        if len(roots) != 1 or next(iter(roots)) in ("", ".", ".."):
            raise ValueError(f"源归档必须只有一个根目录：{archive}")
        destination.mkdir(parents=True, exist_ok=True)
        # 不覆写已有源码改动；不完整解压可重新补齐缺失文件。
        for member in members:
            tarfile.data_filter(member, str(destination))
            existing = destination / member.name
            if member.isfile() and existing.exists():
                with source.extractfile(member) as original:
                    digest = hashlib.file_digest(original, "sha256").hexdigest()
                if not existing.is_file() or file_sha256(existing) != digest:
                    raise ValueError(f"固定源码已被修改：{existing}；保留现场并停止构建")
            else:
                source.extract(member, destination, filter="data")
    return destination / next(iter(roots))


def prepare(work: Path, lock: dict, download: bool) -> tuple[dict[str, Path], dict[str, Path]]:
    sources = {}
    notices = {}
    for item in lock["sources"]:
        archive = acquire(work / "downloads" / f"{item['name']}-{item['revision']}.tar.gz",
                          item["url"], item["sha256"], download)
        sources[item["name"]] = unpack(archive, work / "sources" / item["name"])
    for item in lock["extra_notices"]:
        notices[item["name"]] = acquire(work / "downloads" / item["name"],
                                         item["url"], item["sha256"], download)
    return sources, notices


def render_notices(lock: dict, sources: dict[str, Path], extra: dict[str, Path]) -> str:
    text = (
        "QmClient librime 1.17.0 core runtime\n\n"
        "Plugins: none (RIME_PLUGINS is empty; external plugin loading is disabled).\n"
        "Dependencies are linked statically with the static MSVC runtime.\n"
        "Marisa uses its BSD-2-Clause option. The alternative LGPL text is retained below.\n"
        "OpenCC dictionaries, tools, bundled Marisa, Darts and test libraries are not shipped.\n"
        "RapidJSON JSONChecker is not compiled or shipped.\n\n"
    )
    for item in lock["sources"]:
        text += f"{item['name']} {item['revision']}\n{item['url']}\nSHA-256: {item['sha256']}\n{item['license']}\n"
        for name in [*item["notices"], *item.get("header_notices", [])]:
            content = (sources[item["name"]] / name).read_text(encoding="utf-8-sig")
            if name in item.get("header_notices", []):
                content = re.split(r"^\s*#\s*(?:ifndef|ifdef|include|define)\b", content, maxsplit=1, flags=re.M)[0]
            text += f"\n{'=' * 72}\n{item['name']}/{name}\n{'=' * 72}\n{content.strip()}\n"
        text += "\n"
    for item in lock["extra_notices"]:
        text += f"\n{'=' * 72}\n{item['url']}\nSHA-256: {item['sha256']}\n{'=' * 72}\n"
        text += extra[item["name"]].read_text(encoding="utf-8-sig").strip() + "\n"
    return text


def run_cmake(args: list[str], env: dict[str, str]) -> None:
    wrapper = REPO_ROOT / "qmclient_scripts/cmake-windows.cmd"
    subprocess.run([str(wrapper), *args], cwd=REPO_ROOT, env=env, check=True)


def build(work: Path, sources: dict[str, Path], jobs: int) -> None:
    if sys.platform != "win32":
        raise ValueError("本配方只构建 Windows MSVC x64 Release 核心")
    prefix = work / "deps-prefix"
    include = (prefix / "include").as_posix()
    lib = (prefix / "lib").as_posix()
    env = os.environ.copy()
    # 上游同时支持合并插件和外置插件，必须分别关闭，不能继承宿主环境。
    env["RIME_PLUGINS"] = ""
    env.pop("CL", None)
    env.pop("_CL_", None)
    env.pop("LINK", None)
    common = [
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
        "-DCMAKE_CXX_STANDARD=17", "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_TESTING=OFF",
    ]
    steps = [
        ("leveldb", ["-DLEVELDB_BUILD_TESTS=OFF", "-DLEVELDB_BUILD_BENCHMARKS=OFF",
                     "-DHAVE_SNAPPY=OFF", "-DHAVE_CRC32C=OFF", "-DHAVE_TCMALLOC=OFF"], "install"),
        ("marisa", ["-DENABLE_TOOLS=OFF", "-DENABLE_NATIVE_CODE=OFF"], "install"),
        ("yaml-cpp", ["-DYAML_CPP_BUILD_TESTS=OFF", "-DYAML_CPP_BUILD_TOOLS=OFF",
                      "-DYAML_CPP_BUILD_CONTRIB=OFF", "-DYAML_CPP_INSTALL=ON",
                      "-DYAML_MSVC_SHARED_RT=OFF", "-DYAML_CPP_FORMAT_SOURCE=OFF"], "install"),
    ]
    # OpenCC 的系统 Marisa 分支只查库名；注入明确的 imported target，使头和库
    # 都来自同一份固定 Marisa，避免再构建另一份内嵌版本或查到宿主库。
    hook = work / "opencc-marisa.cmake"
    hook.write_text(
        f'add_library(marisa STATIC IMPORTED)\n'
        f'set_target_properties(marisa PROPERTIES IMPORTED_LOCATION "{lib}/marisa.lib" '
        f'INTERFACE_INCLUDE_DIRECTORIES "{include}")\n', encoding="utf-8", newline="\n")
    steps += [
        ("opencc", ["-DENABLE_GTEST=OFF", "-DENABLE_BENCHMARK=OFF", "-DBUILD_PYTHON=OFF",
                    "-DBUILD_DOCUMENTATION=OFF", "-DENABLE_DARTS=OFF", "-DUSE_SYSTEM_MARISA=ON",
                    "-DUSE_SYSTEM_RAPIDJSON=OFF", f"-DLIBMARISA={lib}/marisa.lib",
                    f"-DCMAKE_PROJECT_opencc_INCLUDE={hook.as_posix()}",
                    f"-DPYTHON_EXECUTABLE={Path(sys.executable).as_posix()}"], "libopencc"),
        ("librime", ["-DBUILD_SHARED_LIBS=ON", "-DBUILD_STATIC=ON", "-DBUILD_TEST=OFF",
                     "-DBUILD_DATA=OFF", "-DBUILD_SAMPLE=OFF", "-DBUILD_SEPARATE_LIBS=OFF",
                     "-DBUILD_MERGED_PLUGINS=OFF", "-DENABLE_EXTERNAL_PLUGINS=OFF",
                     "-DENABLE_LOGGING=OFF", "-DENABLE_THREADING=ON",
                     "-DBoost_NO_SYSTEM_PATHS=ON", "-DBoost_NO_BOOST_CMAKE=ON",
                     f"-DBoost_INCLUDE_DIR={sources['boost'].as_posix()}",
                     f"-DLevelDb_INCLUDE_PATH={include}", f"-DLevelDb_LIBRARY={lib}/leveldb.lib",
                     f"-DMarisa_INCLUDE_PATH={include}", f"-DMarisa_LIBRARY={lib}/marisa.lib",
                     f"-DYamlCpp_INCLUDE_PATH={include}", f"-DYamlCpp_NEW_API={include}",
                     f"-DYamlCpp_LIBRARY={lib}/yaml-cpp.lib",
                     f"-DOpencc_INCLUDE_PATH={include}", f"-DOpencc_LIBRARY={lib}/opencc.lib"], "rime"),
    ]
    for name, options, target in steps:
        directory = (work / "build" / name).as_posix()
        run_cmake(["-G", "Ninja", "-S", sources[name].as_posix(), "-B", directory, *common, *options], env)
        run_cmake(["--build", directory, "--target", target, "-j", str(jobs)], env)
        if name == "opencc":
            # 只构建库目标，不生成或安装 OpenCC 的词典和命令行工具。
            shutil.copy2(work / "build/opencc/src/opencc.lib", prefix / "lib/opencc.lib")
            headers = prefix / "include/opencc"
            headers.mkdir(parents=True, exist_ok=True)
            for pattern in ("*.h", "*.hpp"):
                for header in (sources["opencc"] / "src").glob(pattern):
                    shutil.copy2(header, headers / header.name)
            for name in ("Opencc_Export.h", "opencc_config.h"):
                shutil.copy2(work / "build/opencc/src" / name, headers / name)


def install_runtime(work: Path, sources: dict[str, Path], notices: str) -> Path:
    destination = work / "install"
    (destination / "include").mkdir(parents=True, exist_ok=True)
    shutil.copy2(work / "build/librime/bin/rime.dll", destination / "rime.dll")
    shutil.copy2(sources["librime"] / "src/rime_api.h", destination / "include/rime_api.h")
    (destination / "RIME-LICENSES.txt").write_text(notices, encoding="utf-8", newline="\n")
    shutil.copy2(LOCK_FILE, destination / "rime-core-sources.json")
    receipt = (
        "format=qmc-rime-core-v1\nversion=1.17.0\nplatform=windows-msvc-x64\n"
        "plugins=none\ncrt=MT\n"
        f"source_lock_sha256={file_sha256(LOCK_FILE)}\n"
        f"recipe_sha256={file_sha256(Path(__file__))}\n"
        f"dll_sha256={file_sha256(destination / 'rime.dll')}\n"
        f"header_sha256={file_sha256(destination / 'include/rime_api.h')}\n"
        f"notices_sha256={file_sha256(destination / 'RIME-LICENSES.txt')}\n"
    )
    # 回执最后写入；失败或中断不会产生可被 FindRime 接受的半成品。
    partial = destination / "rime-core-build.txt.part"
    partial.write_text(receipt, encoding="utf-8", newline="\n")
    partial.replace(destination / "rime-core-build.txt")
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--download", action="store_true", help="允许下载固定输入；不代表编译")
    parser.add_argument("--build", action="store_true", help="明确执行串行 MSVC Release 构建")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 6:
        parser.error("--jobs 必须为 1..6；共享构建资源需另行协调")
    try:
        work = args.work_dir.resolve()
        work.relative_to(REPO_ROOT / "tmp")
        lock = json.loads(LOCK_FILE.read_text(encoding="utf-8"))
        sources, extra = prepare(work, lock, args.download)
        notices = render_notices(lock, sources, extra)
        if args.build:
            build(work, sources, args.jobs)
            print(f"核心运行时已准备：{install_runtime(work, sources, notices)}")
        else:
            print(f"固定源码和许可已准备：{work}；尚未编译（显式 --build 后执行）")
    except (OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        parser.exit(1, f"准备失败：{error}\n")


if __name__ == "__main__":
    main()
