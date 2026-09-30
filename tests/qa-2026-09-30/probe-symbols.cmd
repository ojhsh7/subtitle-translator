@echo off
cd /d "%~dp0"
echo B-04 symbol probe (about 5 minutes, no network).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0probe-symbols.ps1"
echo Finished.
pause
