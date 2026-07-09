@echo off
rem Build iwser: locate MSVC C++ tools, set up the environment, then run CMake + Ninja.
setlocal EnableDelayedExpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Install Visual Studio 2022 Build Tools.
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if "%VSPATH%"=="" (
    echo [ERROR] No Visual Studio install with the C++ workload was found.
    echo         Install it once with:
    echo.
    echo   "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vs_installer.exe" modify ^^
    echo     --installPath "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools" ^^
    echo     --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive
    echo.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

rem Generate the app icon (from assets\logo.png if present, else a placeholder).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-icon.ps1" || exit /b 1

cmake --preset msvc-x64 || exit /b 1
cmake --build --preset msvc-x64 || exit /b 1

echo.
echo Build complete: build\iwser.exe
exit /b 0
