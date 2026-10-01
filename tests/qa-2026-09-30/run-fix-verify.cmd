@echo off
chcp 65001 >nul
cd /d "%~dp0"
set QA_ROOT=D:\qa-build\2026-10-01
set QA_RESULTS=%~dp0results-fix
if not exist "%QA_RESULTS%" mkdir "%QA_RESULTS%"
echo started > "%QA_RESULTS%\started.txt"
tasklist /FI "IMAGENAME eq SubtitleTranslator.exe" | find /I "SubtitleTranslator.exe" >nul && (
  echo [!] SubtitleTranslator is running. Close it, then press a key.
  pause
)
echo Re-verifying the fixed working tree (about 20 minutes). Please do not use the PC until it finishes.
echo [1/5] build + existing tests
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-qa.ps1" > "%QA_RESULTS%\console-build.log" 2>&1
echo [2/5] runtime checks
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-runtime.ps1" > "%QA_RESULTS%\console-verify.log" 2>&1
echo [3/5] blank and near-blank images
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0probe-blank.ps1" > "%QA_RESULTS%\console-blank.log" 2>&1
echo [4/5] symbols
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0probe-symbols.ps1" > "%QA_RESULTS%\console-symbols.log" 2>&1
echo [5/5] app self-check (fake game window, hotkeys)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0suite\suite-app.ps1" -Cases A-01,C-01,A-02,A-05 -Tag selfcheck > "%QA_RESULTS%\console-selfcheck.log" 2>&1
echo done > "%QA_RESULTS%\DONE.txt"
echo Finished. Results: %QA_RESULTS%
timeout /t 10 >nul
