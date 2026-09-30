@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo Redeploy + runtime checks (about 6 minutes). Please leave the PC alone until it finishes.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy-qa.ps1"
move /y "%~dp0results\runtime.log" "%~dp0results\runtime-run1.log" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-runtime.ps1"
echo.
echo Finished. Results: %~dp0results
pause
