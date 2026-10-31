param([string]$Manifest, [string]$Out, [string[]]$Langs = @('eng', 'kor'))
# Reads fixture images with Windows.Media.Ocr (built-in Windows OCR) and records text and time.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime]
$null = [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($op, [type]$type) { $t = $asTask.MakeGenericMethod($type).Invoke($null, @($op)); $null = $t.Wait(-1); $t.Result }
$tags = @{ eng = 'en-US'; kor = 'ko' }
$engines = @{}
foreach ($l in $Langs) { $engines[$l] = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage([Windows.Globalization.Language]::new($tags[$l])) }
$dir = Split-Path $Manifest
$rows = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
$results = New-Object System.Collections.ArrayList
$first = $true
foreach ($r in $rows) {
    if ($Langs -notcontains $r.ocr) { continue }
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Join-Path $dir $r.file))) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $result = Await ($engines[$r.ocr].RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    $ms = $watch.Elapsed.TotalMilliseconds
    $text = ($result.Lines | ForEach-Object { $_.Text }) -join "`n"
    $null = $results.Add([ordered]@{ file = $r.file; lang = $r.lang; ocr = $r.ocr; truth = $r.truth; actual = $text; ocrMs = $ms; coldModelLoad = $first; engine = 'WindowsOcr' })
    $first = $false
    $stream.Dispose()
}
[ordered]@{ mode = 'windows-ocr'; rows = $results } | ConvertTo-Json -Depth 5 | Out-File -LiteralPath $Out -Encoding utf8
"rows: $($results.Count)"
