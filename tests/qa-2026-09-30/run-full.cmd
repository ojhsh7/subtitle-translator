@echo off
rem Relaunch minimized so this console does not cover the fake game window or the app.
if not "%~1"=="min" (
  start "qa-full" /min "%~f0" min
  exit /b
)
chcp 65001 >nul
cd /d "%~dp0"
rem Target: the fixed build (HEAD 1c87bd4 + working-tree fixes, snapshot 2026-10-01 03:40).
set QA_ROOT=D:\qa-build\2026-10-01
set QA_RESULTS=%~dp0results-full
if not exist "%QA_RESULTS%" mkdir "%QA_RESULTS%"
echo started > "%QA_RESULTS%\started.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-translate.ps1" > "%QA_RESULTS%\console-translate.log" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -WithKeyCases > "%QA_RESULTS%\console-app.log" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-regress.ps1" > "%QA_RESULTS%\console-regress.log" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-leak.ps1" > "%QA_RESULTS%\console-leak.log" 2>&1
echo done > "%QA_RESULTS%\FULL-RUN-DONE.txt"
exit
