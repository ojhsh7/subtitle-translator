# OCR false-text probe (P-01, P-02). Runs the real OcrService through ocr_benchmark.exe: no network, no settings.
# (subtitle_cli.exe is not used: Smart App Control blocks it on the development PC.)
# Three sets on the same images for every build:
#   symbols - game symbols alone or next to text (from QA B-04): symbols must not become letters
#   keep    - real short lines (はい, OK, 네, Lv.10 ...): must still be read exactly
#   blank   - empty dialogue boxes with a border, frame, noise or gradient: must read nothing
param(
    [Parameter(Mandatory)][string]$Bench,   # ocr_benchmark.exe of the build under test
    [Parameter(Mandatory)][string]$Out,
    [string]$Toolchain = 'D:\SubtitleTranslatorToolchain',
    [string]$Images = 'D:\SubtitleTranslatorToolchain\bench\false-text-images'
)
$ErrorActionPreference = 'Stop'
# ocr_benchmark prints progress on stderr; Windows PowerShell 5.1 turns that into errors under 'Stop'.
function Invoke-Bench([string[]]$arguments) { $ErrorActionPreference = 'Continue'; & $Bench @arguments 2>$null | Out-Null; if ($LASTEXITCODE) { throw "ocr_benchmark failed ($LASTEXITCODE)" } }
$env:PATH = "$Toolchain\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$paddle = "$Toolchain\paddle"; $tess = "$Toolchain\ocr\tesseract.exe"; $tessdata = "$Toolchain\ocr\tessdata"
New-Item -ItemType Directory -Force -Path $Out, $Images | Out-Null
Add-Type -AssemblyName System.Drawing, System.Windows.Forms

$symbolLines = @(
    @{ id = 's1-shapes';   text = '♪ ★ ※ ▶ ▼ ◆ ● ■'; only = $true },
    @{ id = 's1-arrows';   text = '→ ← ↑ ↓ ⇒ ▲'; only = $true },
    @{ id = 's1-brackets'; text = '「 」 『 』 【 】'; only = $true },
    @{ id = 's1-dashes';   text = '…  ―  ・  〜  ｜'; only = $true },
    @{ id = 's1-numbers';  text = '① ② ③ Ⅰ Ⅱ Ⅲ'; only = $true },
    @{ id = 's1-misc';     text = '♥ ☆ ◎ ✕ ○ △'; only = $true },
    @{ id = 's2-next';     text = '▶ 다음' },
    @{ id = 's2-reward';   text = '★ 보상 획득 ★' },
    @{ id = 's2-note';     text = '※ 주의: 저장하지 않은 데이터가 있습니다' },
    @{ id = 's2-quote';    text = '「안녕하세요」' },
    @{ id = 's3-ja-quote'; text = '「こんにちは」' },
    @{ id = 's3-ja-tag';   text = '【重要】セーブしますか？' },
    @{ id = 's3-en-note';  text = '♪ Hello ♪' },
    @{ id = 's4-bar-en';   text = 'HP | MP | SP' },
    @{ id = 's4-bar-ko';   text = 'Lv.10 | 전사' }
)
$keepLines = @(
    @{ id = 'k-hai';   text = 'はい' },  @{ id = 'k-iie';  text = 'いいえ' }, @{ id = 'k-e';    text = 'え？' },
    @{ id = 'k-un';    text = 'うん' },  @{ id = 'k-ok';   text = 'OK' },     @{ id = 'k-yes';  text = 'Yes' },
    @{ id = 'k-no';    text = 'No' },    @{ id = 'k-go';   text = 'Go!' },    @{ id = 'k-hp';   text = 'HP 100' },
    @{ id = 'k-lv';    text = 'Lv.10' }, @{ id = 'k-ne';   text = '네' },     @{ id = 'k-eung'; text = '응' },
    @{ id = 'k-a';     text = '아!' },   @{ id = 'k-ten';  text = '10' }
)
$styles = @(
    @{ id = 'light-outline'; bg = [System.Drawing.Color]::FromArgb(30, 32, 44); fg = [System.Drawing.Color]::White; outline = $true },
    @{ id = 'dark-on-light'; bg = [System.Drawing.Color]::FromArgb(240, 240, 236); fg = [System.Drawing.Color]::Black; outline = $false }
)
function New-LineImage($text, $size, $style, $file) {
    if (Test-Path $file) { return }
    $font = New-Object System.Drawing.Font('Yu Gothic UI', $size, [System.Drawing.GraphicsUnit]::Pixel)
    $measure = [System.Windows.Forms.TextRenderer]::MeasureText($text, $font)
    $bmp = New-Object System.Drawing.Bitmap ($measure.Width + 40), ($measure.Height + 24)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear($style.bg)
    if ($style.outline) {
        foreach ($dx in -1, 0, 1) { foreach ($dy in -1, 0, 1) { if ($dx -or $dy) {
            [System.Windows.Forms.TextRenderer]::DrawText($g, $text, $font, (New-Object System.Drawing.Point (20 + $dx), (12 + $dy)), [System.Drawing.Color]::Black, $style.bg)
        } } }
    }
    [System.Windows.Forms.TextRenderer]::DrawText($g, $text, $font, (New-Object System.Drawing.Point 20, 12), $style.fg, [System.Drawing.Color]::Transparent)
    $g.Dispose(); $font.Dispose(); $bmp.Save($file); $bmp.Dispose()
}
$blankCases = @(
    @{ id = 'B-white-200x60'; w = 200; h = 60; c = [System.Drawing.Color]::White },
    @{ id = 'B-black-200x60'; w = 200; h = 60; c = [System.Drawing.Color]::Black },
    @{ id = 'B-gray-600x120'; w = 600; h = 120; c = [System.Drawing.Color]::FromArgb(40, 44, 60) },
    @{ id = 'B-white-1200x300'; w = 1200; h = 300; c = [System.Drawing.Color]::White },
    @{ id = 'N-white-border-200x60'; w = 200; h = 60; c = [System.Drawing.Color]::White; border = $true },
    @{ id = 'N-dark-frame-820x140'; w = 820; h = 140; c = [System.Drawing.Color]::FromArgb(28, 30, 44); frame = $true },
    @{ id = 'N-white-noise2-200x60'; w = 200; h = 60; c = [System.Drawing.Color]::White; noise = 2 },
    @{ id = 'N-dark-noise4-820x140'; w = 820; h = 140; c = [System.Drawing.Color]::FromArgb(28, 30, 44); noise = 4 },
    @{ id = 'N-gradient-820x140'; w = 820; h = 140; c = [System.Drawing.Color]::FromArgb(20, 24, 40); gradient = $true }
)
function New-BlankImage($k, $file) {
    if (Test-Path $file) { return }
    $b = New-Object System.Drawing.Bitmap $k.w, $k.h; $g = [System.Drawing.Graphics]::FromImage($b); $g.Clear($k.c)
    if ($k.border) { $g.DrawRectangle([System.Drawing.Pens]::Black, 0, 0, $k.w - 1, $k.h - 1) }
    if ($k.frame) { $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(180, 160, 120)), 4; $g.DrawRectangle($pen, 6, 6, $k.w - 12, $k.h - 12); $pen.Dispose() }
    if ($k.gradient) { $br = New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.Rectangle 0, 0, $k.w, $k.h), $k.c, [System.Drawing.Color]::FromArgb(60, 70, 110), 90); $g.FillRectangle($br, 0, 0, $k.w, $k.h); $br.Dispose() }
    $g.Dispose()
    if ($k.noise) {
        $rnd = New-Object System.Random 7
        for ($y = 0; $y -lt $k.h; $y++) { for ($x = 0; $x -lt $k.w; $x++) {
            $p = $b.GetPixel($x, $y); $d = $rnd.Next(-$k.noise, $k.noise + 1)
            $b.SetPixel($x, $y, [System.Drawing.Color]::FromArgb([math]::Max(0, [math]::Min(255, $p.R + $d)), [math]::Max(0, [math]::Min(255, $p.G + $d)), [math]::Max(0, [math]::Min(255, $p.B + $d))))
        } }
    }
    $b.Save($file); $b.Dispose()
}
function Letters($s) { if ($null -eq $s) { return '' }; ($s.Normalize([Text.NormalizationForm]::FormKC) -replace '[^\p{L}\p{N}]', '') }

# One manifest for the PaddleOCR runs (each row carries its own OCR language) and one for Tesseract-only blanks.
$main = New-Object System.Collections.Generic.List[object]
foreach ($set in @(@{ name = 'symbols'; lines = $symbolLines }, @{ name = 'keep'; lines = $keepLines })) {
    foreach ($l in $set.lines) { foreach ($size in 16, 24, 36) { foreach ($st in $styles) {
        $name = "$($l.id)-$size-$($st.id).png"; New-LineImage $l.text $size $st "$Images\$name"
        $langs = if ($l.text -match '\p{IsHangulSyllables}' -or $set.name -eq 'symbols') { 'jpn+eng+kor', 'kor' } else { , 'jpn+eng+kor' }
        foreach ($lang in $langs) {
            $main.Add([ordered]@{ file = $name; ocr = $lang; lang = $set.name; truth = $l.text; set = $set.name; id = $l.id; only = [bool]$l.only; size = $size; style = $st.id })
        }
    } } }
}
$blank = New-Object System.Collections.Generic.List[object]
foreach ($k in $blankCases) {
    $name = "$($k.id).png"; New-BlankImage $k "$Images\$name"
    $row = [ordered]@{ file = $name; ocr = 'jpn+eng+kor'; lang = 'blank'; truth = ''; set = 'blank'; id = $k.id; only = $false; size = 0; style = '' }
    $main.Add($row); $blank.Add($row)
}
# The Tesseract-only run also reads the real short lines: its confidence on them sets how far a filter may go.
foreach ($row in @($main | Where-Object { $_.set -eq 'keep' })) { $blank.Add($row) }
# ConvertTo-Json in Windows PowerShell 5.1 writes UTF-16 by default through Set-Content; write UTF-8 explicitly.
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$Images\manifest-paddle.json", [string]($main.ToArray() | ConvertTo-Json -Depth 3), $utf8)
[IO.File]::WriteAllText("$Images\manifest-tesseract.json", [string]($blank.ToArray() | ConvertTo-Json -Depth 3), $utf8)
Invoke-Bench @('--manifest', "$Images\manifest-paddle.json", '--models', $paddle, '--tesseract', $tess, '--tessdata', $tessdata, '--mode', 'selected', '--output', "$Out\paddle.json")
Invoke-Bench @('--manifest', "$Images\manifest-tesseract.json", '--models', "$Toolchain\no-paddle-models", '--tesseract', $tess, '--tessdata', $tessdata, '--mode', 'selected', '--output', "$Out\tesseract.json")

$rows = New-Object System.Collections.Generic.List[object]
foreach ($run in @(@{ file = 'paddle.json'; engine = 'paddle' }, @{ file = 'tesseract.json'; engine = 'tesseract' })) {
    $data = [IO.File]::ReadAllText("$Out\$($run.file)", $utf8) | ConvertFrom-Json
    foreach ($r in $data.rows) {
        $read = Letters $r.actual
        # Symbols-only: judge the raw text. NFKC would turn a Roman numeral such as "Ⅲ" into Latin "III".
        $ok = if ($r.set -eq 'blank') { [string]::IsNullOrWhiteSpace($r.actual) }
              elseif ($r.only) { "$($r.actual)" -notmatch '\p{L}' }
              else { $read -eq (Letters $r.truth) }
        # Japanese text read with the Korean-only setting is a wrong user setting, not a false-text case.
        $mismatch = $r.ocr -eq 'kor' -and $r.truth -match '[\p{IsHiragana}\p{IsKatakana}\p{IsCJKUnifiedIdeographs}]'
        $rows.Add([pscustomobject]@{ set = $r.set; id = $r.id; truth = $r.truth; size = $r.size; style = $r.style; lang = $r.ocr
            engine = "$($run.engine)->$($r.engine)"; text = ($r.actual -replace "`r?`n", ' / '); ok = $ok; mismatch = $mismatch; error = $r.error
            detectorBoxes = $r.detectorBoxes; tesseractConfidence = $r.tesseractConfidence
            lines = if ($r.paddleLines) { (@($r.paddleLines) | ForEach-Object { '{0} [{1:N3} {2}] {3}' -f $_.text, $_.confidence, ($_.model -replace '_PP-OCRv5_rec_mobile.onnx',''), $(if ($_.pieces) { (@($_.pieces) | ForEach-Object { '{0}:{1:N2}' -f $_[0], $_[1] }) -join ' ' }) }) -join ' | ' } else { '' } })
    }
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 "$Out\false-text-probe.csv"
$summary = [ordered]@{ bench = $Bench; generated = (Get-Date -Format s) }
foreach ($name in 'symbols', 'keep') {
    $s = @($rows | Where-Object { $_.set -eq $name -and -not $_.mismatch -and $_.engine -like 'paddle->*' })
    $summary[$name] = [ordered]@{ runs = $s.Count; ok = @($s | Where-Object ok).Count }
}
$s = @($rows | Where-Object { $_.set -eq 'keep' -and $_.engine -like 'tesseract->*' })
$summary['keep-tesseract'] = [ordered]@{ runs = $s.Count; ok = @($s | Where-Object ok).Count
    lowestConfidenceWhenRight = (@($s | Where-Object { $_.ok -and [double]$_.tesseractConfidence -ge 0 } | ForEach-Object { [double]$_.tesseractConfidence }) | Measure-Object -Minimum).Minimum }
$only = @($rows | Where-Object { $_.set -eq 'symbols' -and $_.id -like 's1-*' })
$summary['symbolsOnly'] = [ordered]@{ runs = $only.Count; lettersAppeared = @($only | Where-Object { -not $_.ok }).Count }
foreach ($engine in 'paddle', 'tesseract') {
    $s = @($rows | Where-Object { $_.set -eq 'blank' -and $_.engine -like "$engine->*" })
    $summary["blank-$engine"] = [ordered]@{ runs = $s.Count; empty = @($s | Where-Object ok).Count }
}
[IO.File]::WriteAllText("$Out\summary.json", [string]($summary | ConvertTo-Json -Depth 4), $utf8)
$summary | ConvertTo-Json -Depth 4
