# X-06 follow-up: what does OCR return for images with no text? (no network, no key)
param([string]$QaRoot = 'D:\qa-build\2026-09-30')
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue
$results = Join-Path $PSScriptRoot 'results'; $dist = Join-Path $QaRoot 'dist'
Add-Type -AssemblyName System.Drawing
$cases = @(
  @{ id = 'B-white-200x60'; w = 200; h = 60; c = [System.Drawing.Color]::White },
  @{ id = 'B-black-200x60'; w = 200; h = 60; c = [System.Drawing.Color]::Black },
  @{ id = 'B-gray-600x120'; w = 600; h = 120; c = [System.Drawing.Color]::FromArgb(40, 44, 60) },
  @{ id = 'B-white-1200x300'; w = 1200; h = 300; c = [System.Drawing.Color]::White }
)
$out = foreach ($k in $cases) {
  $png = "$results\$($k.id).png"
  $b = New-Object System.Drawing.Bitmap $k.w, $k.h; $g = [System.Drawing.Graphics]::FromImage($b); $g.Clear($k.c); $g.Dispose(); $b.Save($png); $b.Dispose()
  foreach ($engine in 'paddle', 'tesseract') {
    $json = & "$dist\subtitle_cli.exe" --input $png --engine $engine --tesseract "$dist\ocr\tesseract.exe" --tessdata "$dist\ocr\tessdata" 2>$null | Out-String
    $code = $LASTEXITCODE; $o = $null; try { $o = $json | ConvertFrom-Json } catch {}
    [pscustomobject]@{ id = $k.id; engine = $engine; exitCode = $code; ocrEngine = $o.ocrEngine; attempts = $o.ocrAttempts; state = $o.state; text = $o.sourceText; pieces = $o.piecesToTranslate }
  }
}
$out | Export-Csv -NoTypeInformation -Encoding UTF8 "$results\blank_probe.csv"
