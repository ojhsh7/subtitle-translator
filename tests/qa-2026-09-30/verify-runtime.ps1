# QA 4단계(일부): Windows 실행 검증. build-qa.ps1 다음에 실행한다.
# - DeepL 호출 없음: subtitle_cli는 --translate 없이만 실행한다(이 옵션은 저장된 실제 키를 쓸 수 있음).
# - 키 값을 읽거나 출력하지 않는다. 사용자 설정은 읽기 전용으로 백업하고 실행 전후를 비교한다.
param([string]$QaRoot = 'D:\qa-build\2026-09-30', [string]$ToolchainRoot = 'D:\SubtitleTranslatorToolchain', [int]$Repeat = 10)
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$here = $PSScriptRoot
$repo = (Get-Item -LiteralPath "$here\..\..").FullName
$results = Join-Path $here 'results'
$backup = Join-Path $here 'settings-backup'
New-Item -ItemType Directory -Force -Path $results, $backup | Out-Null
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue
$dist = Join-Path $QaRoot 'dist'; $cli = "$dist\subtitle_cli.exe"; $app = "$dist\SubtitleTranslator.exe"
$bench = Join-Path $QaRoot 'out\Release\ocr_benchmark.exe'
if (!(Test-Path $cli)) { throw "build-qa.ps1을 먼저 실행하세요: $cli 없음" }
# Qt is NOT added to PATH: the app and CLI must run from the deployed folder alone.
$qtBin = "$ToolchainRoot\Qt\6.8.3\msvc2022_64\bin"
$log = Join-Path $results 'runtime.log'
# Runs a native program with stderr to a file. Avoids PS 5.1 wrapping stderr lines as NativeCommandError.
function Invoke-Native($exe, [string[]]$arguments, $stderrFile) {
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -RedirectStandardError $stderrFile -NoNewWindow -Wait -PassThru
    return $p.ExitCode
}
function Log($m) { $line = "$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ss.fff') $m"; Add-Content -Encoding UTF8 -LiteralPath $log $line; Write-Output $line }
$tess = "$dist\ocr\tesseract.exe"; $tessdata = "$dist\ocr\tessdata"

# 0. 설정 백업(레지스트리만; 키는 자격 증명 저장소에 있고 건드리지 않음)
$regKey = 'HKCU\Software\PersonalTools\SubtitleTranslator'
Remove-Item "$backup\before.reg", "$backup\after.reg" -ErrorAction SilentlyContinue
reg export $regKey "$backup\before.reg" /y *> $null
$regExists = ($LASTEXITCODE -eq 0) -and (Test-Path "$backup\before.reg")
Log "settings backup exit=$LASTEXITCODE (settings key present=$regExists)"
$running = Get-Process -Name SubtitleTranslator -ErrorAction SilentlyContinue
Log "running SubtitleTranslator instances=$(@($running).Count) (다른 사본이 단축키를 잡고 있으면 단축키 등록 결과가 달라짐)"

# 1. 앱 실행 스모크 테스트: 실제 UI 이벤트 루프, OCR·네트워크 없음
#    주의: 앱이 조직·앱 이름을 하드코딩해 실제 사용자 설정을 읽는다(테스트 전용 설정 경로 없음).
#    "키 저장"이 켜져 있으면 실행 중 키가 메모리에 로드되지만 전송은 하지 않는다. 설정 변경은 5단계에서 확인.
$smoke = @()
for ($i = 1; $i -le 5; $i++) {
    $png = "$results\smoke-$i.png"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $app -ArgumentList '--smoke-test', '--screenshot', $png -PassThru -WorkingDirectory $dist
    $ok = $p.WaitForExit(30000); if (!$ok) { $p.Kill() }
    $smoke += [pscustomobject]@{ run = $i; exitCode = $(if ($ok) { $p.ExitCode } else { 'timeout' }); wallMs = $sw.ElapsedMilliseconds; screenshot = (Test-Path $png) }
}
$smoke | Export-Csv -NoTypeInformation -Encoding UTF8 "$results\app_smoke.csv"
Log "app smoke: $(($smoke | ForEach-Object { "$($_.exitCode)/$($_.wallMs)ms" }) -join ', ')"

# 2. CLI 종료 코드 (0/2/3만 안전하게 재현 가능. 4는 저장된 키를 읽고, 5·6은 네트워크가 필요해 제외)
Add-Type -AssemblyName System.Drawing
$blank = "$results\blank-200x60.png"
$bmp = New-Object System.Drawing.Bitmap 200, 60; $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::White); $g.Dispose(); $bmp.Save($blank); $bmp.Dispose()
$notImage = "$results\not-image.png"; Set-Content -LiteralPath $notImage -Value 'not an image'
$fixture = "$repo\tests\fixtures\ocr\outlined-en-18px.png"
$codes = @(
    @{ id = 'X-01'; expect = 2; args = @() },
    @{ id = 'X-02'; expect = 2; args = @('--input', $fixture, '--lang', 'xxx') },
    @{ id = 'X-03'; expect = 2; args = @('--input', $fixture, '--target', 'KR') },
    @{ id = 'X-04'; expect = 2; args = @('--input', $notImage) },
    @{ id = 'X-05'; expect = 2; args = @('--input', "$results\missing.png") },
    @{ id = 'X-06'; expect = 3; args = @('--input', $blank, '--tesseract', $tess, '--tessdata', $tessdata) },
    @{ id = 'X-07'; expect = 0; args = @('--input', $fixture, '--tesseract', $tess, '--tessdata', $tessdata) }
)
$codeRows = foreach ($c in $codes) {
    $out = & $cli @($c.args) 2>$null | Out-String
    $code = $LASTEXITCODE
    $o = $null; try { $o = $out | ConvertFrom-Json } catch {}
    [pscustomobject]@{
        id = $c.id; expected = $c.expect; actual = $code; pass = ($code -eq $c.expect)
        state = $o.state; ocrEngine = $o.ocrEngine; attempts = $o.ocrAttempts
        text = ($o.sourceText -replace "`r?`n", ' / '); pieces = $o.piecesToTranslate
        args = ($c.args -join ' ') -replace [regex]::Escape($repo), '<repo>'
    }
}
$codeRows | Export-Csv -NoTypeInformation -Encoding UTF8 "$results\cli_exit_codes.csv"
Log "cli exit codes: $(($codeRows | ForEach-Object { "$($_.id)=$($_.actual)" }) -join ' ')"

# 3. CLI OCR 반복 측정 (프로세스 시작·모델 로딩 포함 = 콜드 1줄 처리)
#    앱이 켜져 있을 때의 웜 처리 시간과 다르다. 웜 값은 4단계 벤치마크의 warmMedianMs/warmP95Ms를 쓴다.
$cases = @()
Get-ChildItem "$repo\tests\fixtures\ocr" -Filter *.png | ForEach-Object { $cases += @{ set = 'ocr'; file = $_.FullName; lang = 'jpn+eng+kor'; n = $Repeat } }
Get-ChildItem "$repo\tests\fixtures\ocr\scene" -Filter *.png | ForEach-Object { $cases += @{ set = 'scene'; file = $_.FullName; lang = 'jpn+eng+kor'; n = $Repeat } }
$ml = Get-Content "$repo\tests\fixtures\ocr\multilang\manifest.json" -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($r in $ml) {
    $f = "$repo\tests\fixtures\ocr\multilang\$($r.file)"
    $cases += @{ set = 'multilang-selected'; file = $f; lang = $r.ocr; n = $Repeat; expected = $r.truth }
    $cases += @{ set = 'multilang-multi'; file = $f; lang = 'multi'; n = $Repeat; expected = $r.truth }
}
$rows = foreach ($c in $cases) {
    for ($i = 1; $i -le $c.n; $i++) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $json = & $cli --input $c.file --lang $c.lang --tesseract $tess --tessdata $tessdata 2>$null | Out-String
        $code = $LASTEXITCODE; $wall = $sw.ElapsedMilliseconds
        $o = $null; try { $o = $json | ConvertFrom-Json } catch {}
        [pscustomobject]@{
            set = $c.set; file = (Split-Path $c.file -Leaf); lang = $c.lang; run = $i; exitCode = $code; wallMs = $wall
            ocrMs = $o.ocrMs; totalMs = $o.totalMs; engine = $o.ocrEngine; modelRuns = $o.ocrAttempts
            hostPeakMB = if ($o.hostPeakWorkingSetBytes) { [math]::Round($o.hostPeakWorkingSetBytes / 1MB, 1) } else { $null }
            childPeakMB = if ($o.ocrChildPeakWorkingSetBytes) { [math]::Round($o.ocrChildPeakWorkingSetBytes / 1MB, 1) } else { $null }
            expected = $c.expected; text = ($o.sourceText -replace "`r?`n", ' / ')
        }
    }
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 "$results\cli_ocr_runs.csv"
Log "cli ocr runs=$(@($rows).Count) failures=$(@($rows | Where-Object exitCode -ne 0).Count)"

$env:PATH = "$qtBin;$env:PATH" # ocr_benchmark runs from the build folder, not the deployed one
# 4. 672장 합성 픽스처 CER 재측정 (저장소 build\ocr-benchmark, gitignore 대상)
$manifest = "$repo\build\ocr-benchmark\manifest.json"
if ((Test-Path $bench) -and (Test-Path $manifest)) {
    foreach ($mode in 'selected', 'legacy', 'multi') {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $code = Invoke-Native $bench @('--manifest', $manifest, '--models', "$dist\ocr\paddle", '--output', "$results\bench-$mode.json", '--mode', $mode, '--tesseract', $tess, '--tessdata', $tessdata) "$results\bench-$mode.log"
        Log "benchmark $mode exit=$code wall=$([math]::Round($sw.Elapsed.TotalSeconds,1))s"
    }
} else { Log "benchmark skipped: bench=$(Test-Path $bench) manifest=$(Test-Path $manifest)" }
$scene = "$repo\build\scene-benchmark\manifest.json"
if ((Test-Path $bench) -and (Test-Path $scene)) {
    foreach ($mode in 'selected', 'legacy') {
        $code = Invoke-Native $bench @('--manifest', $scene, '--models', "$dist\ocr\paddle", '--output', "$results\scene-$mode.json", '--mode', $mode, '--tesseract', $tess, '--tessdata', $tessdata) "$results\scene-$mode.log"
        Log "scene benchmark $mode exit=$code"
    }
} else { Log "scene benchmark skipped" }

# 5. 설정 변경 여부 확인
reg export $regKey "$backup\after.reg" /y *> $null
$afterExists = ($LASTEXITCODE -eq 0) -and (Test-Path "$backup\after.reg")
if ($regExists -and $afterExists) {
    $same = (Get-FileHash "$backup\before.reg").Hash -eq (Get-FileHash "$backup\after.reg").Hash
    Log "settings unchanged by QA runs=$same"
} elseif (!$regExists -and !$afterExists) {
    Log "settings unchanged by QA runs=True (no settings key before or after)"
} else {
    Log "settings unchanged by QA runs=False (settings key present before=$regExists after=$afterExists)"
}

# 6. 키 형식 문자열 검색 (값은 출력하지 않고 건수만)
$pattern = '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}(:fx)?'
$hits = Get-ChildItem $results, "$QaRoot\out" -Recurse -File -Include *.log, *.txt, *.csv, *.json, *.xml -ErrorAction SilentlyContinue |
    Select-String -Pattern $pattern -ErrorAction SilentlyContinue
Log "key-shaped strings in results and build logs: $(@($hits).Count)"
Log 'verify-runtime done'
