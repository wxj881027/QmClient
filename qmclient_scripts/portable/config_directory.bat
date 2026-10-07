@echo off
if not exist "%~dp0profile\" mkdir "%~dp0profile"
if not exist "%~dp0profile\" exit /b 1
start "" "%~dp0profile"
