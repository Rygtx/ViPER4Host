@echo off
setlocal

rem Locate Visual Studio via vswhere, then enter its MSVC environment.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere not found; install Visual Studio or set VSWHERE manually. >&2
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_ROOT=%%i"
if not defined VS_ROOT (
  echo No Visual Studio installation found. >&2
  exit /b 1
)
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul

rem Ninja ships with Visual Studio but is not on PATH by default.
set "NINJA_DIR=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"

cd /d "%~dp0"

if "%1"=="clean" rmdir /s /q build

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release %2 %3 %4 || exit /b 1
cmake --build build --parallel || exit /b 1
