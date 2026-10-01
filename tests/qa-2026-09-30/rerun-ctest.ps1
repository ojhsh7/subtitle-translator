# Reruns one CTest test of an existing QA build (default: game_windows) and a PS 5.1 parse check of the QA scripts.
param([string]$ToolchainRoot = 'D:\SubtitleTranslatorToolchain', [string]$Test = 'game_windows', [int]$Repeat = 3)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$QaRoot = if ($env:QA_ROOT) { $env:QA_ROOT } else { 'D:\qa-build\2026-09-30' }
$results = if ($env:QA_RESULTS) { $env:QA_RESULTS } else { Join-Path $here 'results' }
New-Item -ItemType Directory -Force -Path $results | Out-Null
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue

# 1) Parse every QA script with this (5.1) parser.
$lines = @("powershell $($PSVersionTable.PSVersion)")
foreach ($f in (Get-ChildItem -LiteralPath $here -Filter *.ps1) + (Get-ChildItem -LiteralPath "$here\suite" -Filter *.ps1)) {
    $t = $null; $e = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$t, [ref]$e)
    if ($e.Count) { $lines += "$($f.Name): $($e.Count) errors"; foreach ($x in $e) { $lines += "  L$($x.Extent.StartLineNumber): $($x.Message)" } }
    else { $lines += "$($f.Name): OK" }
}
$lines | Set-Content -LiteralPath "$results\parse-51.txt" -Encoding UTF8

# 2) Rerun the test with nothing covering the test window (this console is started minimized).
$found = Get-Command cmake -ErrorAction SilentlyContinue
$CMakeExe = if ($found) { $found.Source } else { Join-Path $ToolchainRoot 'python\cmake\data\bin\cmake.exe' }
$ctest = Join-Path (Split-Path $CMakeExe) 'ctest.exe'
# Same environment as build-qa.ps1 (without Qt on PATH the test exe fails with 0xc0000135).
$QtRoot = Join-Path $ToolchainRoot 'Qt\6.8.3\msvc2022_64'
$env:PATH = "$QtRoot\bin;$env:PATH"; $env:QT_PLUGIN_PATH = "$QtRoot\plugins"
if (Test-Path "$ToolchainRoot\ocr\tesseract.exe") { $env:SUBTITLE_TEST_TESSERACT = "$ToolchainRoot\ocr\tesseract.exe"; $env:SUBTITLE_TEST_TESSDATA = "$ToolchainRoot\ocr\tessdata" }
if (Test-Path "$ToolchainRoot\paddle") { $env:SUBTITLE_TEST_PADDLE = "$ToolchainRoot\paddle" }
$bld = Join-Path $QaRoot 'out'
$out = @()
for ($i = 1; $i -le $Repeat; $i++) {
    $log = "$results\ctest-$Test-rerun-$i.log"
    & $ctest --test-dir $bld -C Release -R "^$Test`$" --output-on-failure *> $log
    $out += "run $i exit=$LASTEXITCODE"
}
$out | Set-Content -LiteralPath "$results\ctest-$Test-rerun.txt" -Encoding UTF8
