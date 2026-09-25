:: 请抬头享受阳光｜日子很好 我很我---------致咩子
@echo off
setlocal

if "%~1"=="" goto usage

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDEVCMD="

if exist "%VSWHERE%" (
	for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\Tools\VsDevCmd.bat`) do (
		set "VSDEVCMD=%%I"
	)
)

if not defined VSDEVCMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not defined VSDEVCMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not defined VSDEVCMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not defined VSDEVCMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"

if not defined VSDEVCMD (
	echo Failed to locate VsDevCmd.bat. Install Visual Studio 2022 with MSVC build tools.
	exit /b 1
)

set "FALLBACK_PYTHON="
if exist "%USERPROFILE%\scoop\apps\python\current\python.exe" set "FALLBACK_PYTHON=%USERPROFILE%\scoop\apps\python\current\python.exe"
if not defined FALLBACK_PYTHON if exist "D:\Scoop\apps\python\current\python.exe" set "FALLBACK_PYTHON=D:\Scoop\apps\python\current\python.exe"
if not defined FALLBACK_PYTHON (
	for /f "delims=" %%I in ('where python 2^>nul') do (
		if not defined FALLBACK_PYTHON (
			echo %%~fI | find /I "WindowsApps" >nul
			if errorlevel 1 set "FALLBACK_PYTHON=%%~fI"
		)
	)
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

if defined VSINSTALLDIR (
	if exist "%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "PATH=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
)

set "CMAKE_EXE="
if exist "D:\Scoop\apps\cmake\current\bin\cmake.exe" set "CMAKE_EXE=D:\Scoop\apps\cmake\current\bin\cmake.exe"
if not defined CMAKE_EXE (
	for /f "delims=" %%I in ('where cmake.exe 2^>nul') do (
		if not defined CMAKE_EXE set "CMAKE_EXE=%%~fI"
	)
)
if not defined CMAKE_EXE (
	echo Failed to locate cmake.exe.
	exit /b 1
)

set "CMOUT=%TEMP%\cmake-windows-%RANDOM%.log"
if /I "%~1"=="--build" (
	"%CMAKE_EXE%" %* > "%CMOUT%" 2>&1
) else if /I "%~1"=="-E" (
	"%CMAKE_EXE%" %* > "%CMOUT%" 2>&1
) else if /I "%~1"=="-P" (
	"%CMAKE_EXE%" %* > "%CMOUT%" 2>&1
) else if /I "%~1"=="--install" (
	"%CMAKE_EXE%" %* > "%CMOUT%" 2>&1
) else if /I "%~1"=="--open" (
	"%CMAKE_EXE%" %* > "%CMOUT%" 2>&1
) else if /I "%~1"=="--workflow" (
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

set "RULES_FIXED="
rem Repair rules.ninja again in case configure/build regenerated it during this command.
if defined FALLBACK_PYTHON (
	call "%FALLBACK_PYTHON%" "%~dp0repair_ninja_msvc_prefix.py" %*
) else (
	call py.exe -3 "%~dp0repair_ninja_msvc_prefix.py" %*
)
if not errorlevel 1 (
	set "RULES_FIXED=1"
) else (
	call python "%~dp0repair_ninja_msvc_prefix.py" %*
	if not errorlevel 1 (
		set "RULES_FIXED=1"
	)
)

set "FILTERED="
if defined FALLBACK_PYTHON (
	call "%FALLBACK_PYTHON%" "%~dp0cmake-windows-filter.py" "%CMOUT%"
) else (
	call py.exe -3 "%~dp0cmake-windows-filter.py" "%CMOUT%"
)
if not errorlevel 1 (
	set "FILTERED=1"
) else (
	call python "%~dp0cmake-windows-filter.py" "%CMOUT%"
	if not errorlevel 1 (
		set "FILTERED=1"
	)
)
if not defined FILTERED type "%CMOUT%"
del /Q "%CMOUT%" >nul 2>&1
exit /b %CMRC%

:usage
echo Usage: qmclient_scripts\cmake-windows.cmd [cmake arguments]
echo Daily build: qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target game-client -j 14
echo First-time configure: qmclient_scripts\cmake-windows.cmd -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
exit /b 1
