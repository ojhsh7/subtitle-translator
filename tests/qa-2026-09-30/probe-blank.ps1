# X-06 follow-up: what does OCR return for images with no text? (no network, no key)
param([string]$QaRoot = 'D:\qa-build\2026-09-30')
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue
if ($env:QA_ROOT) { $QaRoot = $env:QA_ROOT }
$results = if ($env:QA_RESULTS) { $env:QA_RESULTS } else { Join-Path $PSScriptRoot 'results' }; New-Item -ItemType Directory -Force -Path $results | Out-Null; $dist = Join-Path $QaRoot 'dist'
Add-Type -AssemblyName System.Drawing
$cases = @(
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
function Paint-Case($k) {
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
  return $b
}
$out = foreach ($k in $cases) {
  $png = "$results\$($k.id).png"
  $b = Paint-Case $k; $b.Save($png); $b.Dispose()
  foreach ($engine in 'paddle', 'tesseract') {
    $json = & "$dist\subtitle_cli.exe" --input $png --engine $engine --tesseract "$dist\ocr\tesseract.exe" --tessdata "$dist\ocr\tessdata" 2>$null | Out-String
    $code = $LASTEXITCODE; $o = $null; try { $o = $json | ConvertFrom-Json } catch {}
    [pscustomobject]@{ id = $k.id; engine = $engine; exitCode = $code; ocrEngine = $o.ocrEngine; attempts = $o.ocrAttempts; state = $o.state; text = $o.sourceText; pieces = $o.piecesToTranslate }
  }
}
$out | Export-Csv -NoTypeInformation -Encoding UTF8 "$results\blank_probe.csv"
