param([string]$ToolchainRoot=(Join-Path $env:LOCALAPPDATA 'SubtitleTranslatorToolchain'))
$ErrorActionPreference='Stop'
$dest=Join-Path $ToolchainRoot 'paddle'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
foreach($model in (Get-Content -LiteralPath "$PSScriptRoot\paddle-models.json" -Raw | ConvertFrom-Json)) {
    $path=Join-Path $dest $model.file
    if((Test-Path -LiteralPath $path) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $model.sha256) { continue }
    $download="$path.download"
    Invoke-WebRequest -Uri $model.url -OutFile $download -UseBasicParsing
    if((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $model.sha256) { throw "Model hash mismatch: $($model.file); not installed." }
    Move-Item -LiteralPath $download -Destination $path -Force
}
Write-Output "Verified PaddleOCR models: $dest"

# Vietnamese needs Tesseract's tone-mark dictionary; the runtime itself is installed separately.
$tess=Join-Path $ToolchainRoot 'ocr\tessdata'
New-Item -ItemType Directory -Force -Path $tess | Out-Null
foreach($model in (Get-Content -LiteralPath "$PSScriptRoot\tesseract-extra-models.json" -Raw | ConvertFrom-Json)) {
    $path=Join-Path $tess $model.file
    if((Test-Path -LiteralPath $path) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $model.sha256) { continue }
    $download="$path.download"
    Invoke-WebRequest -Uri $model.url -OutFile $download -UseBasicParsing
    if((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $model.sha256) { throw "Model hash mismatch: $($model.file)" }
    Move-Item -LiteralPath $download -Destination $path -Force
}
