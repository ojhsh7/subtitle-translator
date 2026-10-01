# Diagnostic: why auto translation did not start from A-09 onward in the full run.
# Checks auto start (Ctrl+Alt+P and --auto) in several states. No key, no translation. Restores settings at the end.
. "$PSScriptRoot\qa-common.ps1"
$Qa.Results = Join-Path $Qa.Results 'diag-auto'; New-Item -ItemType Directory -Force -Path $Qa.Results | Out-Null
$script:LogFile = Join-Path $Qa.Results 'diag-auto.log'
$G = @{ x = 40; y = 1000; w = 820; h = 140 }; $R = Rect 40 1000 820 140
function RegState {
    $c = Get-ItemProperty "$($Qa.RegPath)\capture" -ErrorAction SilentlyContinue
    $h = Get-ItemProperty "$($Qa.RegPath)\hover" -ErrorAction SilentlyContinue
    "screen=[$($c.screen)] geom=[$($c.screenGeometry)] dpr=[$($c.devicePixelRatio)] region=[$($c.region)] hover=[$(($h.PSObject.Properties | Where-Object Name -notlike 'PS*' | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ',')]"
}
function Texts { ((Get-UiTexts) | Where-Object { $_ -match '영역|자동|준비|등록|모니터|마우스|OCR' } | ForEach-Object { $_ -replace '\r?\n', ' / ' }) -join ' | ' }
function Probe([string]$label, [string[]]$appArgs = @(), [bool]$pressP = $true) {
    Log "==== $label"
    Log "registry before start: $(RegState)"
    $pt = [System.Windows.Forms.Cursor]::Position; Log "cursor=($($pt.X),$($pt.Y))"
    [void](Start-App $appArgs); Start-Sleep -Milliseconds 800
    $script:Uia = $null
    Log "after start: autoOn=$(Get-AutoOn) texts=$(Texts)"
    if ($pressP) { Hotkey 'P'; Start-Sleep -Milliseconds 2500 } else { Start-Sleep -Milliseconds 2500 }
    $script:Uia = $null
    $h = Get-Hwnd '자막 번역'
    Log "after P/wait: autoOn=$(Get-AutoOn) minimized=$([QaWin]::IsIconic($h)) ocr=[$(Get-OcrText)] overlay=$((Get-Overlay) -ne [IntPtr]::Zero) texts=$(Texts)"
    Stop-App
    Log "registry after stop: $(RegState)"
}
try {
    Use-QaBaseSettings $R
    [void](Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Follow me to the castle.'); holdMs = 3600000 } 'diag-auto')
    [void][QaWin]::SetCursorPos(1500, 400)
    Probe '1 baseline: fresh settings, cursor away'
    Use-QaBaseSettings $R
    [void][QaWin]::SetCursorPos(560, 1070)
    Probe '2 cursor inside the subtitle region'
    # Reproduce A-08: hover on/off, then a new app instance.
    Use-QaBaseSettings $R
    [void](Start-App); [void][QaWin]::SetCursorPos(1500, 400)
    $script:Uia = $null; $hk = if (((Get-UiTexts) -join ' ') -like '*Ctrl+Alt+F9*') { 'F9' } else { 'H' }
    Hotkey $hk; Start-Sleep -Milliseconds 600; [void][QaWin]::SetCursorPos(330, 1070); Start-Sleep -Milliseconds 2500
    [void][QaWin]::SetCursorPos(560, 1070); Start-Sleep -Milliseconds 2500; Hotkey $hk; Start-Sleep -Milliseconds 500
    Log "A-08 replay done with hover key $hk"
    Stop-App
    Log "registry after A-08 replay (no reset): $(RegState)"
    Probe '3 right after hover replay, settings NOT reset'
    Use-QaBaseSettings $R
    Probe '4 after hover replay, settings reset'
    Use-QaBaseSettings $R
    [void][QaWin]::SetCursorPos(1500, 400)
    Probe '5 --auto, cursor away' @('--auto') $false
} catch { Log "diag error: $($_.Exception.Message) at $($_.InvocationInfo.ScriptLineNumber)" }
finally { Stop-App; Stop-Game; Restore-UserSettings; Log 'diag-auto done' }
