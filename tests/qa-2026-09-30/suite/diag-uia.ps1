# Diagnostic: what UI Automation can read from the app before and during auto translation (main window minimized).
# No key, no translation. Restores the user's settings at the end.
. "$PSScriptRoot\qa-common.ps1"
$Qa.Results = Join-Path $Qa.Results 'diag'; New-Item -ItemType Directory -Force -Path $Qa.Results | Out-Null
$script:LogFile = Join-Path $Qa.Results 'diag-uia.log'
$AE = [System.Windows.Automation.AutomationElement]; $TS = [System.Windows.Automation.TreeScope]
function Dump([string]$label) {
    Log "---- $label"
    $pidCond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $script:AppProc.Id)
    foreach ($view in 'Raw', 'Control') {
        $walker = if ($view -eq 'Raw') { [System.Windows.Automation.TreeWalker]::RawViewWalker } else { [System.Windows.Automation.TreeWalker]::ControlViewWalker }
        $tops = @($AE::RootElement.FindAll($TS::Children, $pidCond))
        Log "$view top-level windows of app: $($tops.Count)"
        foreach ($t in $tops) {
            $c = $t.Current
            Log "  [$($c.ControlType.ProgrammaticName)] '$($c.Name)' offscreen=$($c.IsOffscreen) rect=$($c.BoundingRectangle)"
            $n = 0
            $stack = New-Object System.Collections.Stack; $stack.Push(@($t, 1))
            while ($stack.Count -and $n -lt 200) {
                $item = $stack.Pop(); $el = $item[0]; $d = $item[1]
                $ch = $walker.GetFirstChild($el)
                while ($ch) {
                    $n++
                    try {
                        $cc = $ch.Current; $val = ''
                        $vp = $null; if ($ch.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) { $val = ' value=' + ($vp.Current.Value -replace '\r?\n', ' / ') }
                        $tg = $null; if ($ch.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tg)) { $val += ' toggle=' + $tg.Current.ToggleState }
                        if ($cc.ControlType -eq [System.Windows.Automation.ControlType]::Edit -or $cc.ControlType -eq [System.Windows.Automation.ControlType]::Button -or $cc.ControlType -eq [System.Windows.Automation.ControlType]::Text -or $label -like '*full*') {
                            Log ("  " + ('  ' * $d) + "[$($cc.ControlType.ProgrammaticName)] '$($cc.Name)'$val")
                        }
                    } catch { Log "  error: $($_.Exception.Message)" }
                    if ($d -lt 6) { $stack.Push(@($ch, ($d + 1))) }
                    $ch = $walker.GetNextSibling($ch)
                }
            }
        }
    }
    try {
        $gh = [QaWin]::Find([uint32]$script:Game.Id, 'QA Fake Game', $true); $gr = Get-WinRect $gh
        Log "game window rect=($($gr.Left),$($gr.Top))-($($gr.Right),$($gr.Bottom)) hwnd=$gh"
        $shot = Join-Path $Qa.Results ("region-" + ($label -replace '[^a-z0-9]', '_') + '.png')
        $rr = New-Object QaWin+RECT; $rr.Left = 0; $rr.Top = 900; $rr.Right = 1100; $rr.Bottom = 1240
        Save-Screen $rr $shot; Log "saved screen 0,900-1100,1240 -> $shot"
    } catch { Log "geometry error: $($_.Exception.Message)" }
    $script:Uia = $null
    $o = Get-OcrText; $a = Get-AutoOn
    Log "Get-OcrText=[$o] null=$($null -eq $o) Get-AutoOn=[$a] autoElement='$(if ($script:Uia -and $script:Uia.Auto) { $script:Uia.Auto.Current.ControlType.ProgrammaticName + ' ' + $script:Uia.Auto.Current.Name })' overlay=$((Get-Overlay) -ne [IntPtr]::Zero)"
}
try {
    Use-QaBaseSettings (Rect 40 1000 820 140)
    [void](Start-Game @{ x = 40; y = 1000; w = 820; h = 140; lines = @('The door is locked.'); holdMs = 3600000 } 'diag')
    [void](Start-App)
    Dump 'after start (auto off)'
    Hotkey 'P'; Start-Sleep -Seconds 3
    Dump 'auto on, 3 s'
    Hotkey 'P'; Start-Sleep -Seconds 1
    Dump 'auto off again'
} catch { Log "diag error: $($_.Exception.Message)" }
finally { Stop-App; Stop-Game; Restore-UserSettings; Log 'diag done' }
