# E cases: build older commits in git worktrees outside the repo (D:\qa-wt) and rerun core app cases.
# 1b69b36 has the same code as HEAD (0b8ed75 changed docs only), so "current" results stand for it.
param([string[]]$Commits = @('335dee3', 'e87e9c4', 'd5f87e1'), [string]$ToolchainRoot = 'D:\SubtitleTranslatorToolchain',
      [string[]]$Cases = @('A-01', 'C-01', 'A-03', 'C-02', 'A-05', 'C-03'))
. "$PSScriptRoot\qa-common.ps1"
$Commits = @($Commits | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Cases = @($Cases | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$script:LogFile = Join-Path $Qa.Results 'suite-regress.log'
$QtRoot = Join-Path $ToolchainRoot 'Qt\6.8.3\msvc2022_64'
$found = Get-Command cmake -ErrorAction SilentlyContinue
$CMakeExe = if ($found) { $found.Source } else { Join-Path $ToolchainRoot 'python\cmake\data\bin\cmake.exe' }
$onnx = Join-Path $ToolchainRoot 'onnxruntime'
$wtRoot = 'D:\qa-wt'; New-Item -ItemType Directory -Force -Path $wtRoot | Out-Null
$builds = @()
try {
    foreach ($c in $Commits) {
        $src = Join-Path $wtRoot $c; $out = "$src-out"; $dist = "$src-dist"
        try {
            if (!(Test-Path $src)) { git -C $Qa.Repo worktree add --detach $src $c *> (Join-Path $Qa.Results "worktree-$c.log") }
            $sw = [Diagnostics.Stopwatch]::StartNew()
            & $CMakeExe -S $src -B $out -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" "-DONNXRUNTIME_ROOT=$onnx" -DBUILD_TESTING=OFF *> (Join-Path $Qa.Results "regress-$c-configure.log")
            & $CMakeExe --build $out --config Release --target SubtitleTranslator --parallel *> (Join-Path $Qa.Results "regress-$c-build.log")
            $ok = ($LASTEXITCODE -eq 0) -and (Test-Path "$out\Release\SubtitleTranslator.exe")
            $sec = [math]::Round($sw.Elapsed.TotalSeconds, 1)
            if ($ok) {
                New-Item -ItemType Directory -Force -Path $dist | Out-Null
                Copy-Item "$out\Release\SubtitleTranslator.exe" $dist -Force
                Get-ChildItem "$out\Release" -Filter 'onnxruntime*.dll' -ErrorAction SilentlyContinue | Copy-Item -Destination $dist -Force
                & "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$dist\SubtitleTranslator.exe" *> (Join-Path $Qa.Results "regress-$c-deploy.log")
                Copy-Item "$($Qa.Dist)\ocr" $dist -Recurse -Force
            }
            $builds += [pscustomobject]@{ commit = $c; buildOk = $ok; buildSeconds = $sec; dist = $dist }
            Log "regress build $c ok=$ok ${sec}s"
            if ($ok) { & "$PSScriptRoot\suite-app.ps1" -Cases $Cases -Dist $dist -Tag "regress-$c" -NoRestore | Out-Null }
        } catch { Log "regress $c error: $($_.Exception.Message)" }
    }
} finally {
    Save-Rows $builds 'regress-builds.csv'
    Restore-UserSettings
    foreach ($c in $Commits) { git -C $Qa.Repo worktree remove --force (Join-Path $wtRoot $c) *> $null }
    git -C $Qa.Repo worktree prune *> $null
    Log 'suite-regress done (worktrees removed; builds kept under D:\qa-wt for inspection)'
}
