@echo off
if not "%~1"=="min" (
  start "qa-diag-auto" /min "%~f0" min
  exit /b
)
chcp 65001 >nul
cd /d "%~dp0"
set QA_ROOT=D:\qa-build\2026-10-01
set QA_RESULTS=%~dp0results-full
if exist "%QA_RESULTS%\DONE-diag-auto.txt" del "%QA_RESULTS%\DONE-diag-auto.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\diag-auto.ps1" > "%QA_RESULTS%\console-diag-auto.log" 2>&1
echo done > "%QA_RESULTS%\DONE-diag-auto.txt"
exit
