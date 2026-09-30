# T cases: real DeepL through subtitle_cli --translate (approved 2026-10-01).
# The CLI uses the key stored in Windows Credential Manager; the key is never read or printed here.
param([int]$MaxCalls = 30, [int]$MaxChars = 5000)
. "$PSScriptRoot\qa-common.ps1"
$Qa.Results = Join-Path $Qa.Results 'translate'; New-Item -ItemType Directory -Force -Path $Qa.Results | Out-Null
$script:LogFile = Join-Path $Qa.Results 'suite-translate.log'
$fx = "$($Qa.Repo)\tests\fixtures\ocr"

function New-TextImage($lines, $path, $size = 28) {
    $font = New-Object System.Drawing.Font('Yu Gothic UI', [single]$size, [System.Drawing.GraphicsUnit]::Pixel)
    $text = ($lines -join "`n")
    $m = [System.Windows.Forms.TextRenderer]::MeasureText($text, $font)
    $bmp = New-Object System.Drawing.Bitmap ($m.Width + 40), ($m.Height + 30)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(28, 30, 44))
    [System.Windows.Forms.TextRenderer]::DrawText($g, $text, $font, (New-Object System.Drawing.Point 20, 15), [System.Drawing.Color]::White, [System.Drawing.Color]::Transparent)
    $g.Dispose(); $bmp.Save($path); $bmp.Dispose(); $font.Dispose()
}
New-TextImage @('채주아: Hello!') "$($Qa.Results)\t04-mixed.png"
$alt = @(); for ($l = 0; $l -lt 10; $l++) { $alt += ((1..6 | ForEach-Object { 'abc 가나다' }) -join ' ') }
New-TextImage $alt "$($Qa.Results)\t09-alternating.png" 22

$cases = @(
    @{ id = 'T-01'; file = "$fx\outlined-en-18px.png"; lang = 'eng'; target = 'KO'; n = 5; real = $true },
    @{ id = 'T-02'; file = "$fx\outlined-ja-22px.png"; lang = 'jpn'; target = 'KO'; n = 1; real = $true },
    @{ id = 'T-03'; file = "$fx\outlined-ko-18px.png"; lang = 'kor'; target = 'KO'; n = 1; real = $false },
    @{ id = 'T-04'; file = "$($Qa.Results)\t04-mixed.png"; lang = 'jpn+eng+kor'; target = 'KO'; n = 1; real = $true },
    @{ id = 'T-05'; file = "$fx\outlined-ko-18px.png"; lang = 'kor'; target = 'EN-US'; n = 1; real = $true },
    @{ id = 'T-06'; file = "$fx\outlined-en-18px.png"; lang = 'eng'; target = 'JA'; n = 1; real = $true },
    @{ id = 'T-07'; file = "$fx\multilang\zh-Hans-0-24-dark-on-light-tight.png"; lang = 'chi_sim'; target = 'KO'; n = 1; real = $true },
    @{ id = 'T-08'; file = "$fx\outlined-en-18px.png"; lang = 'eng'; target = 'KO'; n = 1; real = $true; fakeKey = $true },
    @{ id = 'T-09'; file = "$($Qa.Results)\t09-alternating.png"; lang = 'jpn+eng+kor'; target = 'KO'; n = 1; real = $false }
)
$rows = @()
foreach ($c in $cases) {
    for ($i = 1; $i -le $c.n; $i++) {
        $tot = Get-DeeplTotals
        if ($c.real -and ($tot[0] -ge $MaxCalls -or $tot[1] -ge $MaxChars)) { Log "$($c.id) skipped: budget reached ($($tot[0]) calls, $($tot[1]) chars)"; continue }
        if ($c.fakeKey) { $env:SUBTITLE_TRANSLATOR_API_KEY = 'qa-invalid-key-0000:fx' }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $json = & $Qa.Cli --input $c.file --lang $c.lang --target $c.target --translate --tesseract $Qa.Tess --tessdata $Qa.Tessdata 2>$null | Out-String
        $code = $LASTEXITCODE; $wall = $sw.ElapsedMilliseconds
        Remove-Item Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue
        $o = $null; try { $o = $json | ConvertFrom-Json } catch {}
        # translationNetworkUsed is set before TranslationService validates the request; a local rejection sends nothing.
        $sent = [bool]$o.translationNetworkUsed -and !([string]$o.message -like '*너무 깁니다*')
        $chars = if ($sent -and -not $c.fakeKey) { ([string]$o.sourceText).Length } else { 0 }
        if ($sent) { Add-DeeplCall $c.id $chars "exit=$code state=$($o.state)" }
        $rows += [pscustomobject]@{
            case = $c.id; run = $i; target = $c.target; exitCode = $code; state = $o.state; networkUsed = $sent
            pieces = $o.piecesToTranslate; source = ($o.sourceText -replace "`r?`n", ' / '); translated = ($o.translatedText -replace "`r?`n", ' / ')
            detected = $o.detectedSourceLanguage; outboundBytes = $o.outboundTextUtf8Bytes; reportedNetworkUsed = $o.translationNetworkUsed; sourceSent = $o.sourceLanguageSent; ocrMs = $o.ocrMs; translationMs = $o.translationMs; totalMs = $o.totalMs; wallMs = $wall; message = $o.message
        }
        Log "$($c.id) run $i exit=$code state=$($o.state) net=$sent pieces=$($o.piecesToTranslate) translationMs=$($o.translationMs)"
        Start-Sleep -Milliseconds 800
    }
}
Save-Rows $rows 'translate.csv'
$tot = Get-DeeplTotals
Log "suite-translate done: ledger calls=$($tot[0]) chars=$($tot[1])"
