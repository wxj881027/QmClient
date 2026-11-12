@echo off
rem Build the three Windows SDL3 slices into ddnet-libs/sdl/windows.
rem Usage: build_sdl3_prebuilt_windows.cmd <SDL3 source dir>
setlocal enabledelayedexpansion
if "%~1"=="" (
	echo usage: build_sdl3_prebuilt_windows.cmd [SDL3 source dir]
	exit /b 1
)
set "SRC=%~1"
set "ROOT=%~dp0.."
if not defined JOBS set JOBS=4
cd /d "%ROOT%" || exit /b 1

rem 先修复音频设备属性读取，三种 Windows 架构共用同一份实现。
cmake "-DSDL3_SOURCE_DIR=%SRC%" -P "%ROOT%/cmake/PatchSDL3WindowsAudio.cmake" || exit /b 1

set "VC="
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\Tools\VsDevCmd.bat`) do set "VC=%%I"
if not defined VC (
	echo Failed to locate Visual Studio build tools.
	exit /b 1
)
for %%I in ("%VC%") do set "VCALL=%%~dpI..\..\VC\Auxiliary\Build\vcvarsall.bat"

call :build_ninja x64 lib64 || exit /b 1
call :build_ninja x86 lib32 || exit /b 1
call :build_arm64 || exit /b 1

echo windows slices written.
exit /b 0

:build_ninja
set "ARCH=%~1"
set "LIBDIR=%~2"
echo === windows %ARCH% ===
call "%VCALL%" %ARCH% >nul || exit /b 1
cmake -G Ninja -S "%SRC%" -B "tmp\sdl3-prebuilt\windows-%ARCH%" -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF >nul || exit /b 1
cmake --build "tmp\sdl3-prebuilt\windows-%ARCH%" --target SDL3-shared -j %JOBS% >nul || exit /b 1
if not exist "ddnet-libs\sdl\windows\%LIBDIR%" mkdir "ddnet-libs\sdl\windows\%LIBDIR%"
copy /Y "tmp\sdl3-prebuilt\windows-%ARCH%\SDL3.dll" "ddnet-libs\sdl\windows\%LIBDIR%\" >nul
copy /Y "tmp\sdl3-prebuilt\windows-%ARCH%\SDL3.lib" "ddnet-libs\sdl\windows\%LIBDIR%\" >nul
if /i "%ARCH%"=="x64" (
	if not exist "ddnet-libs\sdl\include\windows\SDL3" mkdir "ddnet-libs\sdl\include\windows\SDL3"
	copy /Y "%SRC%\include\SDL3\*.h" "ddnet-libs\sdl\include\windows\SDL3\" >nul
	for /r "tmp\sdl3-prebuilt\windows-%ARCH%" %%C in (SDL_build_config.h) do copy /Y "%%C" "ddnet-libs\sdl\include\windows\SDL3\" >nul
)
exit /b 0

:build_arm64
echo === windows arm64 ===
rem Ninja cannot run the ARM64 rc.exe from an x64 host, so use the VS generator here.
cmake -G "Visual Studio 18 2026" -A ARM64 -S "%SRC%" -B "tmp\sdl3-prebuilt\windows-arm64" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF >nul || exit /b 1
cmake --build "tmp\sdl3-prebuilt\windows-arm64" --config Release --target SDL3-shared -j %JOBS% >nul || exit /b 1
if not exist "ddnet-libs\sdl\windows\libarm64" mkdir "ddnet-libs\sdl\windows\libarm64"
copy /Y "tmp\sdl3-prebuilt\windows-arm64\Release\SDL3.dll" "ddnet-libs\sdl\windows\libarm64\" >nul
copy /Y "tmp\sdl3-prebuilt\windows-arm64\Release\SDL3.lib" "ddnet-libs\sdl\windows\libarm64\" >nul
exit /b 0
