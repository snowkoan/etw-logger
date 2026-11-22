@echo off
rem Shows EtwLogger events live in this console, using tracelog and tracefmt from the
rem Windows SDK. Run it from an elevated prompt.
rem
rem   watch.cmd [ProviderName] [Level]
rem
rem   Level: 1=Critical 2=Error 3=Warning 4=Info 5=Verbose (default)
rem
rem PER-APP: PROVIDER must match ETWLOG_PROVIDER_NAME in EtwLogger.h; setup.ps1 sets both.
setlocal
set "PROVIDER=MyCompany.MyApp"
set "LEVEL=5"
if not "%~1"=="" set "PROVIDER=%~1"
if not "%~2"=="" set "LEVEL=%~2"
set "SESSION=EtwLoggerLive"

rem Use tracelog and tracefmt from PATH, otherwise from the newest installed Windows SDK.
set "TOOLS="
where tracelog >nul 2>&1 && where tracefmt >nul 2>&1 && goto :start
for /d %%D in ("%ProgramFiles(x86)%\Windows Kits\10\bin\10.*") do (
    if exist "%%~D\x64\tracefmt.exe" set "TOOLS=%%~D\x64\"
)
if not defined TOOLS (
    echo tracelog.exe and tracefmt.exe were not found. Install the Windows SDK.
    exit /b 1
)

:start
rem Clean up a session left running by an earlier run that was closed with Ctrl+C.
"%TOOLS%tracelog.exe" -stop %SESSION% >nul 2>&1

"%TOOLS%tracelog.exe" -start %SESSION% -rt -ft 1 -guid *%PROVIDER% -level %LEVEL% >nul
if errorlevel 1 (
    echo Could not start the ETW session ^(tracelog error %errorlevel%^). Run this from an elevated prompt.
    exit /b 1
)

rem Each line: time, PID.TID (hex), the event's fields, and the event name (its level).
set "TRACE_FORMAT_PREFIX=%%4!s! %%8!04X!.%%3!04X! "

rem tracefmt writes text in the ANSI code page, so switch the console to it while watching
rem (otherwise characters such as e-acute show up wrong), then restore the console's own.
for /f "tokens=2 delims=:." %%C in ('chcp') do set /a "CONSOLE_CP=%%C"
for /f "tokens=3" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Nls\CodePage" /v ACP') do set "ANSI_CP=%%A"
if defined ANSI_CP chcp %ANSI_CP% >nul

echo Watching %PROVIDER% at level %LEVEL%. Press any key to stop.
echo --- tracefmt output: a few startup lines, then events as they arrive ---
start "" /b "%TOOLS%tracefmt.exe" -rt %SESSION% -displayonly -nosummary -jsonMeta 2
pause >nul
"%TOOLS%tracelog.exe" -stop %SESSION% >nul
if defined CONSOLE_CP chcp %CONSOLE_CP% >nul
