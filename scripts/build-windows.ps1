param([string]$ToolchainRoot=(Join-Path $env:TEMP 'SubtitleTranslatorToolchain'),[string]$QtRoot,[string]$CMakeExe,[switch]$SkipTests)
$ErrorActionPreference='Stop'
if (!$QtRoot) { $QtRoot=Join-Path $ToolchainRoot 'Qt\6.8.3\msvc2022_64' }
if (!$CMakeExe) { $found=Get-Command cmake -ErrorAction SilentlyContinue; $CMakeExe=if($found){$found.Source}else{Join-Path $ToolchainRoot 'python\cmake\data\bin\cmake.exe'} }
if (!(Test-Path -LiteralPath $CMakeExe) -or !(Test-Path -LiteralPath "$QtRoot\lib\cmake\Qt6")) { throw 'Install CMake and Qt MSVC2022 x64, or specify -CMakeExe and -QtRoot.' }
$projectRoot=(Get-Item -LiteralPath "$PSScriptRoot\..").FullName
$sourceStage=Join-Path $ToolchainRoot 'windows-source'
$buildRoot=Join-Path $ToolchainRoot 'windows-build'
New-Item -ItemType Directory -Force -Path $sourceStage | Out-Null
foreach($entry in @('src','core','tests','CMakeLists.txt')) { Copy-Item -LiteralPath (Join-Path $projectRoot $entry) -Destination $sourceStage -Recurse -Force }
& $CMakeExe -S $sourceStage -B $buildRoot -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if($LASTEXITCODE) { throw 'CMake configure failed.' }
& $CMakeExe --build $buildRoot --config Release --parallel
if($LASTEXITCODE) { throw 'C++ build failed.' }
$env:PATH="$QtRoot\bin;$env:PATH"
$env:QT_PLUGIN_PATH="$QtRoot\plugins"
if(Test-Path -LiteralPath "$ToolchainRoot\ocr\tesseract.exe") { $env:SUBTITLE_TEST_TESSERACT="$ToolchainRoot\ocr\tesseract.exe"; $env:SUBTITLE_TEST_TESSDATA="$ToolchainRoot\ocr\tessdata" }
if(!$SkipTests) {
    & (Join-Path (Split-Path $CMakeExe) 'ctest.exe') --test-dir $buildRoot -C Release --output-on-failure
    if($LASTEXITCODE) { throw 'Tests failed.' }
}
$dist=Join-Path $projectRoot 'dist\windows'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath "$buildRoot\Release\SubtitleTranslator.exe","$buildRoot\Release\subtitle_cli.exe" -Destination $dist -Force
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$dist\SubtitleTranslator.exe" "$dist\subtitle_cli.exe"
if($LASTEXITCODE) { throw 'Qt deployment failed.' }
if(Test-Path -LiteralPath "$ToolchainRoot\ocr\tesseract.exe") {
    $ocrDist=Join-Path $dist 'ocr'
    New-Item -ItemType Directory -Force -Path "$ocrDist\tessdata" | Out-Null
    Copy-Item -LiteralPath "$ToolchainRoot\ocr\tesseract.exe" -Destination $ocrDist -Force
    Get-ChildItem -LiteralPath "$ToolchainRoot\ocr" -Filter '*.dll' | Copy-Item -Destination $ocrDist -Force
    Copy-Item -LiteralPath "$ToolchainRoot\ocr\tessdata\jpn.traineddata" -Destination "$ocrDist\tessdata" -Force
}
Copy-Item -LiteralPath "$projectRoot\docs\THIRD_PARTY_NOTICES.md" -Destination $dist -Force
Write-Output "Runnable prototype: $dist\SubtitleTranslator.exe"
