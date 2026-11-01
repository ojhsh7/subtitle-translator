# Removes what install-windows.ps1 made: the program folder, the Desktop and Start menu shortcuts, and
# the "start with Windows" entry. Settings and the saved DeepL key stay unless -RemoveSettings is given.
param([string]$InstallRoot=(Join-Path $env:LOCALAPPDATA 'Programs\SubtitleTranslator'), [switch]$RemoveSettings)
$ErrorActionPreference='Stop'
# Files of a running copy are locked; ask instead of killing the user's app.
$running=Get-Process -Name SubtitleTranslator -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($InstallRoot) }
if($running) { throw 'Close Subtitle Translator first (tray icon > exit), then run this script again.' }
foreach($shortcut in @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Subtitle Translator.lnk'),
                       (Join-Path ([Environment]::GetFolderPath('Programs')) 'Subtitle Translator.lnk'))) {
    if(Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force; Write-Output "Removed: $shortcut" }
}
$run='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if(Get-ItemProperty -Path $run -Name 'SubtitleTranslator' -ErrorAction SilentlyContinue) {
    Remove-ItemProperty -Path $run -Name 'SubtitleTranslator'; Write-Output 'Removed: start with Windows'
}
if(Test-Path -LiteralPath $InstallRoot) { Remove-Item -LiteralPath $InstallRoot -Recurse -Force; Write-Output "Removed: $InstallRoot" }
if($RemoveSettings) {
    $settings='HKCU:\Software\PersonalTools\SubtitleTranslator'
    if(Test-Path $settings) { Remove-Item -Path $settings -Recurse -Force; Write-Output 'Removed: settings' }
    # The key the app saved in Windows Credential Manager, if the user chose to keep it.
    cmdkey /delete:PersonalTools/SubtitleTranslator/DeepL 2>$null | Out-Null
    Write-Output 'Removed: saved DeepL key (if any)'
}
