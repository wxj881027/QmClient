#!/usr/bin/env bash
# 生成 ddnet-libs/sdl 下的 SDL3 预编译库。
# 用法：在目标平台上运行，只构建当前平台需要的切片。
#   build_sdl3_prebuilt.sh <sdl 源码目录> [平台...]
# 平台取值：windows linux macos ios android webasm
# 说明：
#   * Linux 切片建库前会先打 cmake/PatchSDL3X11.cmake 的修复（预编译后没有源码可改）。
#   * Linux 切片必须在 glibc <= 2.35 的环境里构建（例如 ubuntu-22.04 容器）。在更新的
#     glibc 上构建会把 asinf/atan2f/log10f/acosf/sqrtf 记成新版本符号，CI runner
#     （ubuntu-24.04 / glibc 2.39）链接期就会 undefined reference；用
#     readelf --version-info 复核最高依赖应不超过 2.35。
#   * macOS/iOS 需要 Xcode；webasm 需要 emsdk；Android 需要 NDK。
#   * 产物直接写进 ddnet-libs/sdl/<平台>/...
set -euo pipefail

SRC=${1:?需要 SDL3 源码目录}
shift || true
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST="$ROOT/ddnet-libs/sdl"
SDL_VERSION=3.4.18
JOBS=${JOBS:-4}

copy_headers() {
	local platform=$1
	local build=$2
	mkdir -p "$DEST/include/$platform/SDL3"
	cp "$SRC"/include/SDL3/*.h "$DEST/include/$platform/SDL3/"
	local config
	config=$(find "$build" -name SDL_build_config.h -path "*build_config*" | head -n 1)
	if [[ -z "$config" ]]; then
		echo "missing generated SDL_build_config.h in $build" >&2
		exit 1
	fi
	cp "$config" "$DEST/include/$platform/SDL3/SDL_build_config.h"
}

build_windows() {
	# x64/x86 走 Ninja，arm64 走 VS 生成器；细节在 .cmd 里，避免手写两套环境。
	cmd //c "$ROOT/qmclient_scripts/build_sdl3_prebuilt_windows.cmd" "$SRC"
}

build_linux() {
	local out="$ROOT/tmp/sdl3-prebuilt/linux64"
	cmake -DSDL3_SOURCE_DIR="$SRC" -P "$ROOT/cmake/PatchSDL3X11.cmake"
	cmake -G Ninja -S "$SRC" -B "$out" -DCMAKE_BUILD_TYPE=Release \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
	cmake --build "$out" --target SDL3-shared -j "$JOBS"
	mkdir -p "$DEST/linux/lib64"
	# 运行时文件名由 SONAME 决定（libSDL3.so.0），链接期需要 libSDL3.so。
	cp -L "$out/libSDL3.so.0" "$DEST/linux/lib64/libSDL3.so.0"
	cp -L "$out/libSDL3.so" "$DEST/linux/lib64/libSDL3.so"
	copy_headers linux "$out"
}

build_macos() {
	# 先出 universal framework，再用 lipo 拆出单架构副本，省掉两次完整编译。
	local out="$ROOT/tmp/sdl3-prebuilt/macos"
	cmake -G Ninja -S "$SRC" -B "$out" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_FRAMEWORK=ON \
		-DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
	cmake --build "$out" --target SDL3-shared -j "$JOBS"

	local fw="$out/SDL3.framework"
	for dir in libfat lib64 libarm64; do
		mkdir -p "$DEST/mac/$dir"
		rm -rf "${DEST:?}/mac/$dir/SDL3.framework"
		ditto "$fw" "$DEST/mac/$dir/SDL3.framework"
	done
	lipo "$DEST/mac/lib64/SDL3.framework/Versions/A/SDL3" -thin x86_64 \
		-output "$DEST/mac/lib64/SDL3.framework/Versions/A/SDL3.x64"
	mv "$DEST/mac/lib64/SDL3.framework/Versions/A/SDL3.x64" "$DEST/mac/lib64/SDL3.framework/Versions/A/SDL3"
	lipo "$DEST/mac/libarm64/SDL3.framework/Versions/A/SDL3" -thin arm64 \
		-output "$DEST/mac/libarm64/SDL3.framework/Versions/A/SDL3.arm64"
	mv "$DEST/mac/libarm64/SDL3.framework/Versions/A/SDL3.arm64" "$DEST/mac/libarm64/SDL3.framework/Versions/A/SDL3"
	copy_headers mac "$out"
}

build_ios() {
	# 设备与模拟器各编一次，再合成 xcframework（切片名与 CMake 里的 IOS_XCFRAMEWORK_SLICE 对应）。
	local device="$ROOT/tmp/sdl3-prebuilt/ios-device"
	local sim="$ROOT/tmp/sdl3-prebuilt/ios-simulator"
	cmake -G Ninja -S "$SRC" -B "$device" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
	cmake --build "$device" --target SDL3-static -j "$JOBS"
	cmake -G Ninja -S "$SRC" -B "$sim" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
		-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
	cmake --build "$sim" --target SDL3-static -j "$JOBS"

	mkdir -p "$DEST/ios"
	rm -rf "${DEST:?}/ios/libSDL3.xcframework"
	xcodebuild -create-xcframework \
		-library "$device/libSDL3.a" \
		-library "$sim/libSDL3.a" \
		-output "$DEST/ios/libSDL3.xcframework" >/dev/null
	copy_headers ios "$device"
}

build_android() {
	local ndk=${ANDROID_NDK_HOME:?需要设置 ANDROID_NDK_HOME}
	for abi in arm64-v8a armeabi-v7a x86_64 x86; do
		local out="$ROOT/tmp/sdl3-prebuilt/android/$abi"
		cmake -G Ninja -S "$SRC" -B "$out" \
			-DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
			-DANDROID_ABI=$abi -DANDROID_PLATFORM=android-24 \
			-DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON \
			-DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
		cmake --build "$out" --target SDL3-static -j "$JOBS"
		case $abi in
		arm64-v8a) dir=libarm64 ;;
		armeabi-v7a) dir=libarm ;;
		x86_64) dir=lib64 ;;
		x86) dir=lib32 ;;
		esac
		mkdir -p "$DEST/android/$dir"
		cp "$out/libSDL3.a" "$DEST/android/$dir/libSDL3.a"
		if [[ $abi == arm64-v8a ]]; then copy_headers android "$out"; fi
	done
	mkdir -p "$DEST/java"
	cp -R "$SRC/android-project/app/src/main/java/org" "$DEST/java/"
}

build_webasm() {
	local out="$ROOT/tmp/sdl3-prebuilt/webasm"
	emcmake cmake -G Ninja -S "$SRC" -B "$out" -DCMAKE_BUILD_TYPE=Release \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF
	cmake --build "$out" --target SDL3-static -j "$JOBS"
	mkdir -p "$DEST/webasm/libwasm"
	cp "$out/libSDL3.a" "$DEST/webasm/libwasm/libSDL3.a"
	copy_headers webasm "$out"
}

PLATFORMS=("$@")
if [[ ${#PLATFORMS[@]} -eq 0 ]]; then PLATFORMS=(current); fi
for p in "${PLATFORMS[@]}"; do
	case $p in
	current)
		case "$(uname -s)" in
		MINGW*|MSYS*|CYGWIN*) build_windows ;;
		Linux*) build_linux ;;
		Darwin*) build_macos ;;
		*) echo "unsupported platform" >&2; exit 1 ;;
		esac ;;
	windows) build_windows ;;
	linux) build_linux ;;
	macos) build_macos ;;
	ios) build_ios ;;
	android) build_android ;;
	webasm) build_webasm ;;
	*) echo "unknown platform: $p" >&2; exit 1 ;;
	esac
done

echo "$SDL_VERSION" > "$DEST/VERSION"
echo "SDL3 prebuilt libraries written to $DEST"
