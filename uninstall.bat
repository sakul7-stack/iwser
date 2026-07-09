@echo off
rem Uninstall iwser (runs the PowerShell uninstaller from a neutral directory
rem so the app folder can be removed even when this script lives inside it).
set "PS=%~dp0uninstall.ps1"
cd /d "%TEMP%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS%"
pause
