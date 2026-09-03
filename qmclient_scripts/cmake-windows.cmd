@echo off
setlocal

if "%~1"=="" goto usage

set "VSWHERE="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSWHERE if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDEVCMD="

if defined VSWHERE (
	for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\Tools\VsDevCmd.bat`) do set "VSDEVCMD=%%I"
)

if not defined VSDEVCMD for %%I in (BuildTools Community Professional Enterprise) do if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%I\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\%%I\Common7\Tools\VsDevCmd.bat"
if not defined VSDEVCMD for %%I in (BuildTools Community Professional Enterprise) do if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%I\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%I\Common7\Tools\VsDevCmd.bat"
if not defined VSDEVCMD (
	echo Failed to locate VsDevCmd.bat. Install Visual Studio 2022 with MSVC build tools.
	exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%~fI"
if not defined CMAKE_EXE (
	echo Failed to locate cmake.exe.
	exit /b 1
)

set "CMOUT=%TEMP%\cmake-windows-%RANDOM%.log"
if /I "%~1"=="--build" (
	call py.exe -3 "%~dp0repair_ninja_msvc_prefix.py" %*
	if errorlevel 1 (
		echo Failed to prepare Ninja/MSVC dependency prefix.
		exit /b 1
	)
	"%CMAKE_EXE%" %* > "%CMOUT%" 2>&1
) else (
	"%CMAKE_EXE%" -Wno-dev %* > "%CMOUT%" 2>&1
)
set "CMRC=%errorlevel%"
if not "%CMRC%"=="0" (
	type "%CMOUT%"
	del /Q "%CMOUT%" >nul 2>&1
	exit /b %CMRC%
)

call py.exe -3 "%~dp0repair_ninja_msvc_prefix.py" %*
set "REPAIR_RC=%errorlevel%"
if not "%REPAIR_RC%"=="0" (
	type "%CMOUT%"
	del /Q "%CMOUT%" >nul 2>&1
	exit /b %REPAIR_RC%
)
call py.exe -3 "%~dp0cmake-windows-filter.py" "%CMOUT%"
set "FILTER_RC=%errorlevel%"
if not "%FILTER_RC%"=="0" type "%CMOUT%"
del /Q "%CMOUT%" >nul 2>&1
if not "%FILTER_RC%"=="0" exit /b %FILTER_RC%
exit /b %CMRC%

:usage
echo Usage: qmclient_scripts\cmake-windows.cmd [cmake arguments]
echo Daily build: qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target game-client --parallel 14
echo First-time configure: qmclient_scripts\cmake-windows.cmd -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
exit /b 1
