@echo off
chcp 65001 >nul
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq SubtitleTranslator.exe" | find /I "SubtitleTranslator.exe" >nul && (
  echo [!] SubtitleTranslator is running. Close it first so hotkey tests are not affected.
  pause
)
echo QA build + tests: the tests move the mouse and show windows. Do not use the PC until it finishes.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-qa.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-runtime.ps1"
echo.
echo QA finished. Results: %~dp0results
pause
