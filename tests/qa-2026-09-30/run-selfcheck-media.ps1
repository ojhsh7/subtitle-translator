# Black-box self-check for the media-modes branch: the same suite-app cases as run-selfcheck2.cmd
# (A-01, C-01, A-02, A-05) against a copy of dist\windows, with the isolated QA settings profile.
# Usage: run-selfcheck-media.ps1 -Tag baseline   (copies dist\windows to D:\qa-build\media-<Tag>\dist)
param([Parameter(Mandatory)][string]$Tag, [string]$QaParent = 'D:\qa-build', [int]$DelaySec = 5)
$ErrorActionPreference = 'Stop'
$repo = (Get-Item -LiteralPath "$PSScriptRoot\..\..").FullName
$root = Join-Path $QaParent "media-$Tag"
$dist = Join-Path $root 'dist'
if (Get-Process SubtitleTranslator -ErrorAction SilentlyContinue) { throw 'Close every running SubtitleTranslator first (hotkeys are global).' }
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -Path (Join-Path $repo 'dist\windows\*') -Destination $dist -Recurse -Force
$env:QA_ROOT = $root
$env:QA_RESULTS = Join-Path $repo "build\impl\selfcheck-$Tag"
New-Item -ItemType Directory -Force -Path $env:QA_RESULTS | Out-Null
Start-Sleep -Seconds $DelaySec
$ErrorActionPreference = 'Continue'
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\suite\suite-app.ps1" -Cases A-01,C-01,A-02,A-05 -Tag $Tag *> (Join-Path $env:QA_RESULTS 'console.log')
Get-ChildItem -LiteralPath (Join-Path $env:QA_RESULTS 'suite') -Recurse -Filter 'summary.csv' | ForEach-Object { Get-Content -LiteralPath $_.FullName -Encoding UTF8 }
