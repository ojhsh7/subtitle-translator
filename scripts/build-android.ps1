param(
    [string]$ToolchainRoot=(Join-Path $env:TEMP 'SubtitleTranslatorToolchain'),
    [string]$AndroidSdk=(Join-Path $env:LOCALAPPDATA 'Android\Sdk'),
    [string]$JavaHome='C:\Program Files\Android\Android Studio\jbr',
    [switch]$BuildTests
)
$ErrorActionPreference='Stop'
$projectRoot=(Get-Item -LiteralPath "$PSScriptRoot\..").FullName
$stage=Join-Path $ToolchainRoot 'android-stage'
New-Item -ItemType Directory -Force -Path "$stage\platforms" | Out-Null
Copy-Item -LiteralPath "$projectRoot\core" -Destination $stage -Recurse -Force
Copy-Item -LiteralPath "$projectRoot\platforms\android" -Destination "$stage\platforms" -Recurse -Force
$env:JAVA_HOME=$JavaHome
$env:ANDROID_HOME=$AndroidSdk
Push-Location -LiteralPath "$stage\platforms\android"
try {
    $tasks=@('assembleDebug')
    if($BuildTests) { $tasks+='assembleDebugAndroidTest' }
    & '.\gradlew.bat' @tasks --console=plain --no-daemon
    if($LASTEXITCODE) { throw 'Android build failed.' }
} finally { Pop-Location }
$dist=Join-Path $projectRoot 'dist\android'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath "$stage\platforms\android\app\build\outputs\apk\debug\app-debug.apk" -Destination "$dist\SubtitleTranslator-debug.apk" -Force
Write-Output "Debug APK: $dist\SubtitleTranslator-debug.apk"
