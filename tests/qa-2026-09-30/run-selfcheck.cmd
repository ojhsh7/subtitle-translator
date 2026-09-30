@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo QA suite self-check (about 2 minutes). Please do not use the PC until it finishes.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -Cases A-01,C-01,A-02 -Tag selfcheck
echo Finished.
timeout /t 5 >nul
