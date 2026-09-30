@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo Full QA run (about 2 hours 40 minutes). Please do not use the PC until it finishes.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-translate.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -WithKeyCases
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-regress.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-leak.ps1"
echo done > "%~dp0results\suite\FULL-RUN-DONE.txt"
echo Finished.
timeout /t 10 >nul
