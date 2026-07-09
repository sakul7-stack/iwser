@echo off
rem Build (if needed) and install iwser for the current user.
setlocal
if not exist "%~dp0build\iwser.exe" call "%~dp0build.bat" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
echo.
pause
