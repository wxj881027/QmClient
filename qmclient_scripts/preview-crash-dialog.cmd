@echo off
setlocal

set "QM_PREVIEW_BUILD_DIR=%~1"
if not defined QM_PREVIEW_BUILD_DIR set "QM_PREVIEW_BUILD_DIR=cmake-build-release"
set "QM_PREVIEW_TYPE=%~2"
if not defined QM_PREVIEW_TYPE set "QM_PREVIEW_TYPE=graphics"
set "QM_PREVIEW_CLIENT=%~dp0..\%QM_PREVIEW_BUILD_DIR%\DDNet.exe"

if not exist "%QM_PREVIEW_CLIENT%" (
	echo Preview client not found: "%QM_PREVIEW_CLIENT%"
	echo Build it first with qmclient_scripts\cmake-windows.cmd --build %QM_PREVIEW_BUILD_DIR% --target game-client -j 14
	exit /b 1
)

"%QM_PREVIEW_CLIENT%" --qm-preview-crash-dialog "%QM_PREVIEW_TYPE%"
exit /b %errorlevel%
