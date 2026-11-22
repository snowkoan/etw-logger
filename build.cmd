@echo off
rem Builds out\EtwLoggerTest.exe with the MSVC command-line tools (x64).
rem   build.cmd           Debug build (log messages are also sent to the debugger)
rem   build.cmd release   Release build
rem Running this from a Developer Command Prompt skips the slow Visual Studio environment setup.
setlocal
cd /d "%~dp0"

set "CONFIG=Debug"
if /i "%~1"=="release" set "CONFIG=Release"

where cl >nul 2>&1
if not errorlevel 1 (
    echo [%TIME%] Using the cl.exe already on PATH.
    goto :build
)

echo [%TIME%] Looking for Visual Studio...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR (
    echo Visual Studio with the C++ tools was not found.
    exit /b 1
)
echo [%TIME%] Found %VSDIR%
echo [%TIME%] Setting up the Visual Studio build environment
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 || exit /b 1
echo [%TIME%] Build environment ready.

:build
set "FLAGS=/nologo /std:c++latest /permissive- /W4 /WX /EHsc /utf-8 /Zi"
if "%CONFIG%"=="Release" (
    set "FLAGS=%FLAGS% /O2 /MT /DNDEBUG"
) else (
    set "FLAGS=%FLAGS% /Od /MTd"
)

if not exist out mkdir out
echo [%TIME%] Compiling %CONFIG% build...
cl %FLAGS% /Iinclude tests\EtwLoggerTest.cpp /Foout\ /Fdout\ /Feout\EtwLoggerTest.exe /link /DEBUG
if errorlevel 1 (
    echo [%TIME%] Build failed.
    exit /b 1
)
echo [%TIME%] Built out\EtwLoggerTest.exe (%CONFIG%).
