# QA clean build + existing tests. Copy of scripts/build-windows.ps1 with output paths moved
# outside the repository (the original writes dist\windows in the repo and reuses the toolchain build folder).
# Never reads or prints a DeepL key. Removes key variables from this process before running anything.
param(
    [string]$ToolchainRoot = 'D:\SubtitleTranslatorToolchain',
    [string]$QaRoot = 'D:\qa-build\2026-09-30'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Get-Item -LiteralPath "$here\..\..").FullName
if ($env:QA_ROOT) { $QaRoot = $env:QA_ROOT }
$results = if ($env:QA_RESULTS) { $env:QA_RESULTS } else { Join-Path $here 'results' }
New-Item -ItemType Directory -Force -Path $results | Out-Null
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue

$QtRoot = Join-Path $ToolchainRoot 'Qt\6.8.3\msvc2022_64'
$found = Get-Command cmake -ErrorAction SilentlyContinue
$CMakeExe = if ($found) { $found.Source } else { Join-Path $ToolchainRoot 'python\cmake\data\bin\cmake.exe' }
$onnx = Join-Path $ToolchainRoot 'onnxruntime'
foreach ($p in @($CMakeExe, "$QtRoot\lib\cmake\Qt6", "$onnx\include\onnxruntime_cxx_api.h")) {
    if (!(Test-Path -LiteralPath $p)) { throw "Missing toolchain item: $p" }
}
if (Test-Path -LiteralPath $QaRoot) { throw "QaRoot already exists (clean build needs a new folder): $QaRoot" }
$src = Join-Path $QaRoot 'src'; $bld = Join-Path $QaRoot 'out'; $dist = Join-Path $QaRoot 'dist'
New-Item -ItemType Directory -Force -Path $src, $dist, "$src\tests" | Out-Null
foreach ($e in 'src', 'core', 'CMakeLists.txt') { Copy-Item -LiteralPath (Join-Path $repo $e) -Destination $src -Recurse }
Get-ChildItem -LiteralPath "$repo\tests" | Where-Object { $_.Name -notlike 'qa-*' } | Copy-Item -Destination "$src\tests" -Recurse

# PS 5.1: a native command writing to stderr under 'Stop' aborts the script (windeployqt did). Continue from here.
$ErrorActionPreference = 'Continue'
$sw = [Diagnostics.Stopwatch]::StartNew()
& $CMakeExe -S $src -B $bld -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" "-DONNXRUNTIME_ROOT=$onnx" *> "$results\build-configure.log"
$configureOk = ($LASTEXITCODE -eq 0)
$configureSec = [math]::Round($sw.Elapsed.TotalSeconds, 1)
$buildOk = $false
if ($configureOk) {
    & $CMakeExe --build $bld --config Release --parallel *> "$results\build.log"
    $buildOk = ($LASTEXITCODE -eq 0)
}
$buildSec = [math]::Round($sw.Elapsed.TotalSeconds - $configureSec, 1)

# Warnings: MSBuild repeats lines, so count unique "file(line): warning CODE" entries.
$warnLines = @()
if (Test-Path "$results\build.log") {
    $warnLines = Select-String -LiteralPath "$results\build.log" -Pattern ':\s*warning\s+([A-Z]+\d+)' |
        ForEach-Object { ($_.Line -replace '\s*\[[^\]]*\]\s*$', '').Trim() } | Sort-Object -Unique
}
$warnByCode = @{}
foreach ($w in $warnLines) { if ($w -match 'warning\s+([A-Z]+\d+)') { $warnByCode[$Matches[1]] = 1 + [int]$warnByCode[$Matches[1]] } }
$warnLines | Set-Content -Encoding UTF8 "$results\build-warnings.txt"

# Existing tests (same environment variables as build-windows.ps1).
$env:PATH = "$QtRoot\bin;$env:PATH"; $env:QT_PLUGIN_PATH = "$QtRoot\plugins"
if (Test-Path "$ToolchainRoot\ocr\tesseract.exe") { $env:SUBTITLE_TEST_TESSERACT = "$ToolchainRoot\ocr\tesseract.exe"; $env:SUBTITLE_TEST_TESSDATA = "$ToolchainRoot\ocr\tessdata" }
if (Test-Path "$ToolchainRoot\paddle") { $env:SUBTITLE_TEST_PADDLE = "$ToolchainRoot\paddle" }
$testSec = $null
if ($buildOk) {
    $ctest = Join-Path (Split-Path $CMakeExe) 'ctest.exe'
    $t = [Diagnostics.Stopwatch]::StartNew()
    & $ctest --test-dir $bld -C Release --output-on-failure --output-junit "$results\ctest-junit.xml" *> "$results\ctest.log"
    $testSec = [math]::Round($t.Elapsed.TotalSeconds, 1)
    Get-ChildItem -LiteralPath $bld -Filter '*.txt' | Where-Object { $_.Name -notlike 'CMake*' } | Copy-Item -Destination $results -Force
}

# Runnable copy outside the repo (same layout as dist\windows).
$deployOk = $false
if ($buildOk) {
    Copy-Item "$bld\Release\SubtitleTranslator.exe", "$bld\Release\subtitle_cli.exe" -Destination $dist
    Copy-Item "$onnx\lib\onnxruntime.dll", "$onnx\lib\onnxruntime_providers_shared.dll" -Destination $dist
    New-Item -ItemType Directory -Force "$dist\ocr\paddle", "$dist\ocr\tessdata" | Out-Null
    foreach ($m in (Get-Content "$repo\scripts\paddle-models.json" -Raw | ConvertFrom-Json)) {
        $path = Join-Path $ToolchainRoot "paddle\$($m.file)"
        if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $m.sha256) { throw "Model hash mismatch: $($m.file)" }
        Copy-Item $path "$dist\ocr\paddle"
    }
    & "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$dist\SubtitleTranslator.exe" "$dist\subtitle_cli.exe" *> "$results\windeployqt.log"
    $deployOk = ($LASTEXITCODE -eq 0)
    if (Test-Path "$ToolchainRoot\ocr\tesseract.exe") {
        Copy-Item "$ToolchainRoot\ocr\tesseract.exe" "$dist\ocr"
        Get-ChildItem "$ToolchainRoot\ocr" -Filter '*.dll' | Copy-Item -Destination "$dist\ocr"
        foreach ($m in 'jpn', 'eng', 'kor', 'chi_sim', 'chi_tra', 'vie') {
            $p = "$ToolchainRoot\ocr\tessdata\$m.traineddata"; if (Test-Path $p) { Copy-Item $p "$dist\ocr\tessdata" }
        }
    }
}

function SizeOf($path) { if (Test-Path $path) { (Get-Item $path).Length } else { $null } }
$models = @{}; Get-ChildItem "$dist\ocr\paddle" -ErrorAction SilentlyContinue | ForEach-Object { $models[$_.Name] = $_.Length }
$dlls = @{}; Get-ChildItem $dist -Filter '*.dll' -ErrorAction SilentlyContinue | ForEach-Object { $dlls[$_.Name] = $_.Length }
[ordered]@{
    head = (git -C $repo rev-parse HEAD)
    uncommitted = @(git -C $repo status --porcelain --untracked-files=no).Count
    qaRoot = $QaRoot
    configureOk = $configureOk; buildOk = $buildOk; deployOk = $deployOk
    configureSeconds = $configureSec; buildSeconds = $buildSec; testSeconds = $testSec
    warningsUnique = $warnLines.Count; warningsByCode = $warnByCode
    exeBytes = SizeOf "$dist\SubtitleTranslator.exe"; cliBytes = SizeOf "$dist\subtitle_cli.exe"
    dllBytes = $dlls; paddleModelBytes = $models
    distTotalBytes = (Get-ChildItem $dist -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$results\build.json"
Write-Output "QA build done. buildOk=$buildOk tests=$testSec s. Results: $results"
