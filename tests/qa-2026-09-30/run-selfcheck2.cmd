@echo off
rem Relaunch minimized so this console does not cover test windows or the fake game window.
if not "%~1"=="min" (
  start "qa-selfcheck2" /min "%~f0" min
  exit /b
)
chcp 65001 >nul
cd /d "%~dp0"
set QA_ROOT=D:\qa-build\2026-10-01
set QA_RESULTS=%~dp0results-fix
if exist "%QA_RESULTS%\DONE2.txt" del "%QA_RESULTS%\DONE2.txt"
echo started > "%QA_RESULTS%\started2.txt"
rem Give the operator 8 s to minimize other windows.
timeout /t 8 /nobreak >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rerun-ctest.ps1" > "%QA_RESULTS%\console-ctest-rerun.log" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -Cases A-01,C-01,A-02,A-05 -Tag selfcheck > "%QA_RESULTS%\console-selfcheck.log" 2>&1
echo done > "%QA_RESULTS%\DONE2.txt"
exit
