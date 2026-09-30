# B-04: 특수기호를 글자로 잘못 읽는지 확인 (CLI만, --translate 없음 = DeepL 호출 없음, 설정 변경 없음)
param([string]$QaRoot = 'D:\qa-build\2026-09-30', [int]$Repeat = 3)
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue
$results = Join-Path $PSScriptRoot 'results'; $img = Join-Path $results 'symbols'
New-Item -ItemType Directory -Force -Path $img | Out-Null
$dist = Join-Path $QaRoot 'dist'; $cli = "$dist\subtitle_cli.exe"; $tess = "$dist\ocr\tesseract.exe"; $tessdata = "$dist\ocr\tessdata"
Add-Type -AssemblyName System.Drawing, System.Windows.Forms

$lines = @(
    @{ set = 'S1'; id = 's1-shapes';   text = '♪ ★ ※ ▶ ▼ ◆ ● ■' },
    @{ set = 'S1'; id = 's1-arrows';   text = '→ ← ↑ ↓ ⇒ ▲' },
    @{ set = 'S1'; id = 's1-brackets'; text = '「 」 『 』 【 】' },
    @{ set = 'S1'; id = 's1-dashes';   text = '…  ―  ・  〜  ｜' },
    @{ set = 'S1'; id = 's1-numbers';  text = '① ② ③ Ⅰ Ⅱ Ⅲ' },
    @{ set = 'S1'; id = 's1-misc';     text = '♥ ☆ ◎ ✕ ○ △' },
    @{ set = 'S2'; id = 's2-next';     text = '▶ 다음' },
    @{ set = 'S2'; id = 's2-reward';   text = '★ 보상 획득 ★' },
    @{ set = 'S2'; id = 's2-note';     text = '※ 주의: 저장하지 않은 데이터가 있습니다' },
    @{ set = 'S2'; id = 's2-quote';    text = '「안녕하세요」' },
    @{ set = 'S3'; id = 's3-ja-quote'; text = '「こんにちは」' },
    @{ set = 'S3'; id = 's3-ja-tag';   text = '【重要】セーブしますか？' },
    @{ set = 'S3'; id = 's3-en-note';  text = '♪ Hello ♪' },
    @{ set = 'S4'; id = 's4-bar-en';   text = 'HP | MP | SP' },
    @{ set = 'S4'; id = 's4-bar-ko';   text = 'Lv.10 | 전사' }
)
$styles = @(
    @{ id = 'light-outline'; bg = [System.Drawing.Color]::FromArgb(30, 32, 44); fg = [System.Drawing.Color]::White; outline = $true },
    @{ id = 'dark-on-light'; bg = [System.Drawing.Color]::FromArgb(240, 240, 236); fg = [System.Drawing.Color]::Black; outline = $false }
)
$fontName = 'Yu Gothic UI'  # 한글·가나·기호 모두 GDI 글꼴 대체로 그려짐
$files = @()
foreach ($l in $lines) { foreach ($size in 16, 24, 36) { foreach ($st in $styles) {
    $font = New-Object System.Drawing.Font($fontName, $size, [System.Drawing.GraphicsUnit]::Pixel)
    $measure = [System.Windows.Forms.TextRenderer]::MeasureText($l.text, $font)
    $w = $measure.Width + 40; $h = $measure.Height + 24
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear($st.bg)
    $pt = New-Object System.Drawing.Point 20, 12
    if ($st.outline) {
        foreach ($dx in -1, 0, 1) { foreach ($dy in -1, 0, 1) { if ($dx -or $dy) {
            [System.Windows.Forms.TextRenderer]::DrawText($g, $l.text, $font, (New-Object System.Drawing.Point (20 + $dx), (12 + $dy)), [System.Drawing.Color]::Black, $st.bg)
        } } }
    }
    [System.Windows.Forms.TextRenderer]::DrawText($g, $l.text, $font, $pt, $st.fg, [System.Drawing.Color]::Transparent)
    $g.Dispose(); $font.Dispose()
    $file = "$img\$($l.id)-$size-$($st.id).png"; $bmp.Save($file); $bmp.Dispose()
    $files += @{ line = $l; size = $size; style = $st.id; file = $file }
} } }

$rows = foreach ($f in $files) { foreach ($lang in 'jpn+eng+kor', 'kor') { for ($i = 1; $i -le $Repeat; $i++) {
    $json = & $cli --input $f.file --lang $lang --tesseract $tess --tessdata $tessdata 2>$null | Out-String
    $code = $LASTEXITCODE; $o = $null; try { $o = $json | ConvertFrom-Json } catch {}
    [pscustomobject]@{
        set = $f.line.set; id = $f.line.id; truth = $f.line.text; size = $f.size; style = $f.style; lang = $lang; run = $i
        exitCode = $code; engine = $o.ocrEngine; attempts = $o.ocrAttempts; text = ($o.sourceText -replace "`r?`n", ' / '); pieces = $o.piecesToTranslate
    }
} } }
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 "$results\symbol_probe.csv"
"symbol probe rows=$(@($rows).Count)" | Set-Content -Encoding UTF8 "$results\symbol_probe.log"
