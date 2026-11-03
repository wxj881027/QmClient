#!/bin/bash
# QmClient Android 构建环境（MSYS2 / Git Bash）。
#
# 用法（用 source，变量才会留在当前 shell）：
#   source qmclient_scripts/android/android-env.sh
#   bash qmclient_scripts/android/cmake_android.sh arm64 QmClient com.qmclient.client Debug tmp/android-build
#
# 可覆盖的默认值：
#   QM_JAVA_HOME         JDK 21 根目录，默认 C:/Android/Java/jdk-21.0.12.1+1
#   QM_ANDROID_HOME      Android SDK 根目录，默认 C:/Android/Sdk
#   ANDROID_NDK_VERSION  NDK 版本目录名，默认 27.3.13750724（与 CI 一致）
#   BUILD_FLAGS          并行参数，默认 "-j 6"（见 .agents/machine.local.json）

_qm_env_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." &> /dev/null && pwd)"

# Windows 形式的路径给 CMake / Gradle 用，POSIX 形式给 PATH 用。
_qm_posix() {
	if command -v cygpath > /dev/null 2>&1; then cygpath -u "$1"; else printf '%s' "$1"; fi
}

export QM_JAVA_HOME="${QM_JAVA_HOME:-C:/Android/Java/jdk-21.0.12.1+1}"
export QM_ANDROID_HOME="${QM_ANDROID_HOME:-C:/Android/Sdk}"
export ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-27.3.13750724}"

export JAVA_HOME="${QM_JAVA_HOME}"
# _build_common.sh 要求 ANDROID_HOME 用正斜杠。
export ANDROID_HOME="${QM_ANDROID_HOME}"
export ANDROID_NDK_HOME="${ANDROID_HOME}/ndk/${ANDROID_NDK_VERSION}"
export ANDROID_NDK_ROOT="${ANDROID_NDK_HOME}"

# cargo-ndk 装在 rustup 的 CARGO_HOME/bin，默认不在 PATH 里。
if [ -n "${CARGO_HOME:-}" ]; then
	export PATH="$(_qm_posix "${CARGO_HOME}")/bin:${PATH}"
fi
export PATH="$(_qm_posix "${JAVA_HOME}")/bin:$(_qm_posix "${ANDROID_HOME}")/platform-tools:$(_qm_posix "${ANDROID_HOME}")/cmdline-tools/latest/bin:${PATH}"

export BUILD_FLAGS="${BUILD_FLAGS:--j 6}"

# 可选：把编译临时目录固定到工作区内。默认沿用系统临时目录；
# 某些 Windows 机器上 clang 在系统临时目录建 delete-on-close 文件会被拒绝，
# 这时用 QM_ANDROID_TMP_OVERRIDE=1 打开。
if [ "${QM_ANDROID_TMP_OVERRIDE:-0}" = "1" ]; then
	export QM_ANDROID_TMP="${_qm_env_dir}/tmp/android-tmp"
	mkdir -p "${QM_ANDROID_TMP}"
	export TMP="$(cygpath -w "${QM_ANDROID_TMP}")"
	export TEMP="${TMP}"
	export TMPDIR="${QM_ANDROID_TMP}"
fi

_qm_bad=0
for _t in cmake ninja python3 git cargo rustc; do
	command -v "${_t}" > /dev/null 2>&1 || { echo "[android-env] 缺少命令：${_t}" >&2; _qm_bad=1; }
done
command -v cargo-ndk > /dev/null 2>&1 || { echo "[android-env] 缺少 cargo-ndk：cargo install cargo-ndk --version 4.1.2 --locked" >&2; _qm_bad=1; }
command -v java > /dev/null 2>&1 || { echo "[android-env] PATH 里找不到 java（检查 QM_JAVA_HOME）" >&2; _qm_bad=1; }
[ -d "${ANDROID_NDK_HOME}" ] || { echo "[android-env] 找不到 NDK：${ANDROID_NDK_HOME}" >&2; _qm_bad=1; }

if [ "${_qm_bad}" = "0" ]; then
	echo "[android-env] JDK ${JAVA_HOME}"
	echo "[android-env] SDK ${ANDROID_HOME}"
	echo "[android-env] NDK ${ANDROID_NDK_VERSION}"
	echo "[android-env] 构建目录请用 tmp/android-build（见 docs）"
fi

unset _qm_env_dir _qm_bad _t
