# Re-runs only the deploy step of build-qa.ps1 (first run stopped at windeployqt) and writes build.json.
param([string]$ToolchainRoot = 'D:\SubtitleTranslatorToolchain', [string]$QaRoot = 'D:\qa-build\2026-09-30')
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$repo = (Get-Item -LiteralPath "$here\..\..").FullName
$results = Join-Path $here 'results'
$QtRoot = Join-Path $ToolchainRoot 'Qt\6.8.3\msvc2022_64'
$onnx = Join-Path $ToolchainRoot 'onnxruntime'
$bld = Join-Path $QaRoot 'out'; $dist = Join-Path $QaRoot 'dist'
$buildOk = Test-Path "$bld\Release\SubtitleTranslator.exe"; $configureOk = $buildOk
$configureSec = $null; $buildSec = $null; $testSec = $null
$warnLines = @(Get-Content "$results\build-warnings.txt" -ErrorAction SilentlyContinue | Where-Object { $_ }); $warnByCode = @{}
# Runnable copy outside the repo (same layout as dist\windows).
$deployOk = $false
if ($buildOk) {
    Copy-Item "$bld\Release\SubtitleTranslator.exe", "$bld\Release\subtitle_cli.exe" -Destination $dist -Force
    Copy-Item "$onnx\lib\onnxruntime.dll", "$onnx\lib\onnxruntime_providers_shared.dll" -Destination $dist
    New-Item -ItemType Directory -Force "$dist\ocr\paddle", "$dist\ocr\tessdata" | Out-Null
    foreach ($m in (Get-Content "$repo\scripts\paddle-models.json" -Raw | ConvertFrom-Json)) {
        $path = Join-Path $ToolchainRoot "paddle\$($m.file)"
        if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $m.sha256) { throw "Model hash mismatch: $($m.file)" }
        Copy-Item $path "$dist\ocr\paddle" -Force
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
    head = $(try { git -C $repo rev-parse HEAD } catch { (Get-Content "$repo\.git\refs\heads\ocr-accuracy-foreign-only") })
    uncommitted = $(try { @(git -C $repo status --porcelain --untracked-files=no).Count } catch { "git unavailable" })
    qaRoot = $QaRoot
    configureOk = $configureOk; buildOk = $buildOk; deployOk = $deployOk
    configureSeconds = $configureSec; buildSeconds = $buildSec; testSeconds = $testSec
    warningsUnique = $warnLines.Count; warningsByCode = $warnByCode
    exeBytes = SizeOf "$dist\SubtitleTranslator.exe"; cliBytes = SizeOf "$dist\subtitle_cli.exe"
    dllBytes = $dlls; paddleModelBytes = $models
    distTotalBytes = (Get-ChildItem $dist -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$results\build.json"
Write-Output "QA build done. buildOk=$buildOk tests=$testSec s. Results: $results"
