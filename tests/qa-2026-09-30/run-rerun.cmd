@echo off
if not "%~1"=="min" (
  start "qa-rerun" /min "%~f0" min
  exit /b
)
chcp 65001 >nul
cd /d "%~dp0"
set QA_ROOT=D:\qa-build\2026-10-01
set QA_RESULTS=%~dp0results-full
if exist "%QA_RESULTS%\DONE-rerun.txt" del "%QA_RESULTS%\DONE-rerun.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -Cases A-09,A-11,A-12,C-03,C-04,C-07,C-08,C-09,D-02,D-04,T-10,A-04 -WithKeyCases -Tag rerun > "%QA_RESULTS%\console-rerun.log" 2>&1
echo done > "%QA_RESULTS%\DONE-rerun.txt"
exit
