@echo off
if not "%~1"=="min" (
  start "qa-rerun2" /min "%~f0" min
  exit /b
)
chcp 65001 >nul
cd /d "%~dp0"
set QA_ROOT=D:\qa-build\2026-10-01
set QA_RESULTS=%~dp0results-full
if exist "%QA_RESULTS%\DONE-rerun3.txt" del "%QA_RESULTS%\DONE-rerun3.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -Cases A-04 -WithKeyCases -Tag rerun3 > "%QA_RESULTS%\console-rerun3.log" 2>&1
echo done > "%QA_RESULTS%\DONE-rerun3.txt"
exit
