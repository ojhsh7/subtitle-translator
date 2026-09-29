param([string]$RunRoot=(Join-Path $env:TEMP 'SubtitleTranslatorRun'))
$ErrorActionPreference='Stop'
$projectRoot=(Get-Item -LiteralPath "$PSScriptRoot\..").FullName
$package=Join-Path $projectRoot 'dist\windows'
if(!(Test-Path -LiteralPath "$package\SubtitleTranslator.exe")) { throw 'Run build-windows.ps1 first.' }
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
Get-ChildItem -LiteralPath $package | Copy-Item -Destination $RunRoot -Recurse -Force
# The user invokes this launcher to use the visible interactive app.
Start-Process -FilePath "$RunRoot\SubtitleTranslator.exe" -WorkingDirectory $RunRoot
