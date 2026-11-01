param([string]$InstallRoot=(Join-Path $env:LOCALAPPDATA 'Programs\SubtitleTranslator'))
$ErrorActionPreference='Stop'
$projectRoot=(Get-Item -LiteralPath "$PSScriptRoot\..").FullName
$package=Join-Path $projectRoot 'dist\windows'
if(!(Test-Path -LiteralPath "$package\SubtitleTranslator.exe")) { throw 'Run build-windows.ps1 first.' }
# Files of a running copy are locked; ask instead of killing the user's app.
$running=Get-Process -Name SubtitleTranslator -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($InstallRoot) }
if($running) { throw 'Close the running Subtitle Translator first, then run this script again.' }
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
Get-ChildItem -LiteralPath $package | Copy-Item -Destination $InstallRoot -Recurse -Force
# Per-user shortcut on the real Desktop (works when OneDrive redirects it).
# Per-user shortcuts on the real Desktop (works when OneDrive redirects it) and in the Start menu,
# so searching "Subtitle" finds the app.
$shell=New-Object -ComObject WScript.Shell
$startMenu=Join-Path ([Environment]::GetFolderPath('Programs')) 'Subtitle Translator.lnk'
foreach($shortcutPath in @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Subtitle Translator.lnk'), $startMenu)) {
    $shortcut=$shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath=Join-Path $InstallRoot 'SubtitleTranslator.exe'
    $shortcut.Arguments='--auto' # Start auto translation right away when a region is saved.
    $shortcut.WorkingDirectory=$InstallRoot
    $shortcut.IconLocation="$(Join-Path $InstallRoot 'SubtitleTranslator.exe'),0"
    $shortcut.Description='Game subtitle translator (auto mode)'
    $shortcut.Save()
    Write-Output "Shortcut: $shortcutPath"
}
Write-Output "Installed: $InstallRoot"
