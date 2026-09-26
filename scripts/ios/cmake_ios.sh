#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
# shellcheck source=scripts/compile_libs/_build_common.sh
source "${SCRIPT_DIR}/../compile_libs/_build_common.sh"

if [ $# -ne 5 ]; then
	log_error "Usage: scripts/ios/cmake_ios.sh <device/sim-arm64/sim-x86_64/sim> <App name> <Bundle id> <Debug/Release> <Build folder>"
	exit 1
fi

IOS_BUILD=$1
APP_NAME=$2
BUNDLE_ID=$3
BUILD_TYPE=$4
BUILD_FOLDER=$5
IOS_USE_ASSET_CATALOG=${IOS_USE_ASSET_CATALOG:-ON}

assert_ios_sdk_found

case "${IOS_BUILD}" in
device)
	IOS_SYSROOT="iphoneos"
	IOS_ARCH="arm64"
	IOS_RUST_TARGET="aarch64-apple-ios"
	;;
sim-arm64|simulator-arm64)
	IOS_SYSROOT="iphonesimulator"
	IOS_ARCH="arm64"
	IOS_RUST_TARGET="aarch64-apple-ios-sim"
	;;
sim-x86_64|simulator-x86_64)
	IOS_SYSROOT="iphonesimulator"
	IOS_ARCH="x86_64"
	IOS_RUST_TARGET="x86_64-apple-ios"
	;;
sim|simulator)
	IOS_SYSROOT="iphonesimulator"
	IOS_ARCH="$(uname -m)"
	if [ "${IOS_ARCH}" = "arm64" ]; then
		IOS_RUST_TARGET="aarch64-apple-ios-sim"
	else
		IOS_RUST_TARGET="x86_64-apple-ios"
	fi
	;;
*)
	log_error "Unsupported iOS build target: ${IOS_BUILD}"
	exit 1
	;;
esac

cmake -S . -B "${BUILD_FOLDER}" -G Xcode \
	-DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
	-DCMAKE_SYSTEM_NAME=iOS \
	-DCMAKE_OSX_SYSROOT="${IOS_SYSROOT}" \
	-DCMAKE_OSX_ARCHITECTURES="${IOS_ARCH}" \
	-DCMAKE_OSX_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET}" \
	-DCMAKE_RUST_COMPILER_TARGET="${IOS_RUST_TARGET}" \
	-DCLIENT_EXECUTABLE="${APP_NAME}" \
	-DIOS_BUNDLE_IDENTIFIER="${BUNDLE_ID}" \
	-DPREFER_BUNDLED_LIBS=ON \
	-DSERVER=OFF \
	-DTOOLS=OFF \
	-DVULKAN=OFF \
	-DVIDEORECORDER=OFF \
	-DIOS_USE_ASSET_CATALOG="${IOS_USE_ASSET_CATALOG}" \
	-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
	-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
	-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=""

cmake --build "${BUILD_FOLDER}" --config "${BUILD_TYPE}" --target game-client
