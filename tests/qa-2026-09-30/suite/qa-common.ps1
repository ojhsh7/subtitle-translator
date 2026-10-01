# Shared helpers for the QA suite. Dot-source: . "$PSScriptRoot\qa-common.ps1"
# Never reads or prints the DeepL key. Settings are changed only through Set-QaSettings and
# always restored by Restore-UserSettings (called from each suite's finally block).
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName System.Drawing, System.Windows.Forms, UIAutomationClient, UIAutomationTypes

if (-not ('QaWin' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class QaWin {
    [DllImport("kernel32.dll")] static extern void GetSystemTimePreciseAsFileTime(out long t);
    public static long Now() { long t; GetSystemTimePreciseAsFileTime(out t); return t; } // 100ns ticks
    public static double Ms(long a, long b) { return (b - a) / 10000.0; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    public static IntPtr Find(uint pid, string title, bool visibleOnly) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != pid) return true;
            var sb = new StringBuilder(256); GetWindowText(h, sb, 256);
            if (sb.ToString() != title) return true;
            if (visibleOnly && !IsWindowVisible(h)) return true;
            found = h; return false;
        }, IntPtr.Zero);
        return found;
    }
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr h, int id, uint mods, uint vk);
    public static void Key(byte vk, bool ctrl, bool alt) {
        if (ctrl) keybd_event(0x11, 0, 0, UIntPtr.Zero);
        if (alt) keybd_event(0x12, 0, 0, UIntPtr.Zero);
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        keybd_event(vk, 0, 2, UIntPtr.Zero);
        if (alt) keybd_event(0x12, 0, 2, UIntPtr.Zero);
        if (ctrl) keybd_event(0x11, 0, 2, UIntPtr.Zero);
    }
    public static void Drag(int x0, int y0, int x1, int y1) {
        SetCursorPos(x0, y0); System.Threading.Thread.Sleep(80);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(80);
        for (int i = 1; i <= 10; i++) { SetCursorPos(x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10); System.Threading.Thread.Sleep(20); }
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }
}
'@
}
[void][QaWin]::SetProcessDPIAware()

# ---------- paths ----------
$Qa = @{}
$Qa.Root = Split-Path $PSScriptRoot -Parent                       # tests\qa-2026-09-30
$Qa.Repo = (Get-Item -LiteralPath "$($Qa.Root)\..\..").FullName
$Qa.Results = if ($env:QA_RESULTS) { Join-Path $env:QA_RESULTS 'suite' } else { Join-Path $Qa.Root 'results\suite' }
$Qa.Dist = if ($env:QA_ROOT) { Join-Path $env:QA_ROOT 'dist' } else { 'D:\qa-build\2026-09-30\dist' }
$Qa.App = "$($Qa.Dist)\SubtitleTranslator.exe"
$Qa.Cli = "$($Qa.Dist)\subtitle_cli.exe"
$Qa.Tess = "$($Qa.Dist)\ocr\tesseract.exe"
$Qa.Tessdata = "$($Qa.Dist)\ocr\tessdata"
$Qa.Backup = Join-Path $Qa.Root 'settings-backup\before.reg'
$Qa.RegKey = 'HKCU\Software\PersonalTools\SubtitleTranslator'
$Qa.RegPath = 'HKCU:\Software\PersonalTools\SubtitleTranslator'
New-Item -ItemType Directory -Force -Path $Qa.Results | Out-Null
Remove-Item Env:DEEPL_API_KEY, Env:SUBTITLE_TRANSLATOR_API_KEY -ErrorAction SilentlyContinue

$script:LogFile = Join-Path $Qa.Results 'suite.log'
function Log($m) { $line = "$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ss.fff') $m"; Add-Content -Encoding UTF8 -LiteralPath $script:LogFile $line; Write-Output $line }
function Save-Rows($rows, $name) { if ($rows) { @($rows) | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $Qa.Results $name) } }
function Stat($values) {
    $v = @($values | Where-Object { $_ -ne $null } | Sort-Object)
    if ($v.Count -eq 0) { return [pscustomobject]@{ n = 0; median = $null; p95 = $null; max = $null } }
    $mid = [math]::Floor(($v.Count - 1) / 2)
    $median = if ($v.Count % 2) { $v[$mid] } else { ($v[$mid] + $v[$mid + 1]) / 2 }
    [pscustomobject]@{ n = $v.Count; median = [math]::Round($median, 1); p95 = [math]::Round($v[[math]::Min($v.Count - 1, [int][math]::Floor($v.Count * 0.95))], 1); max = [math]::Round($v[-1], 1) }
}

# ---------- DeepL call ledger ----------
$Qa.Ledger = Join-Path $Qa.Root 'deepl_calls.csv'  # shared across builds so the budget is counted once
function Add-DeeplCall($case, $chars, $status) {
    if (!(Test-Path $Qa.Ledger)) { 'time,case,chars,status' | Set-Content -Encoding UTF8 $Qa.Ledger }
    "$(Get-Date -Format o),$case,$chars,$status" | Add-Content -Encoding UTF8 $Qa.Ledger
}
function Get-DeeplTotals { if (!(Test-Path $Qa.Ledger)) { return @(0, 0) }; $r = @(Import-Csv $Qa.Ledger); @($r.Count, ($r | Measure-Object chars -Sum).Sum) }

# ---------- settings ----------
function Restore-UserSettings {
    Stop-App
    reg delete $Qa.RegKey /f *> $null
    reg import $Qa.Backup *> $null
    Log "settings restored from before.reg (import exit=$LASTEXITCODE)"
}
function Set-QaSettings([hashtable]$values) {
    # values: 'group/name' = string. Unlisted values keep what is there.
    foreach ($k in $values.Keys) {
        $parts = $k -split '/'
        $path = if ($parts.Count -eq 2) { "$($Qa.RegPath)\$($parts[0])" } else { $Qa.RegPath }
        $name = $parts[-1]
        if (!(Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
        New-ItemProperty -Path $path -Name $name -Value ([string]$values[$k]) -PropertyType String -Force | Out-Null
    }
}
function Use-QaBaseSettings([string]$region, [bool]$withKey = $false, [string]$languages = 'jpn+eng+kor') {
    reg delete $Qa.RegKey /f *> $null
    reg import $Qa.Backup *> $null
    Set-QaSettings @{
        'setupComplete' = 'true'
        'capture/screen' = (Get-ItemProperty "$($Qa.RegPath)\capture" -ErrorAction SilentlyContinue).screen
        'capture/screenGeometry' = "@Rect(0 0 $([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width) $([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height))"
        'capture/devicePixelRatio' = '1'
        'capture/region' = $region
        'cursor/size' = '@Size(640 160)'
        'ocr/executable' = ($Qa.Tess -replace '\\', '/')
        'ocr/tessdata' = ($Qa.Tessdata -replace '\\', '/')
        'ocr/languages' = $languages
        'ocr/engine' = 'paddle'
        'translation/target' = 'KO'
        'translation/consent' = $(if ($withKey) { 'true' } else { 'false' })
        'translation/rememberKey' = $(if ($withKey) { 'true' } else { 'false' })
        'display/inlineCaptions' = 'true'
    }
    New-ItemProperty -Path "$($Qa.RegPath)\ocr" -Name 'layout' -Value 0 -PropertyType DWord -Force | Out-Null
}
function Rect($x, $y, $w, $h) { "@Rect($x $y $w $h)" }

# ---------- app control ----------
$script:AppProc = $null; $script:Uia = $null
function Start-App([string[]]$extra = @(), [int]$waitMs = 15000) {
    Stop-App
    # A leftover instance from any path (seen in the full run: pid 74968) holds the global hotkeys; new instances then cannot register them.
    Get-Process -Name SubtitleTranslator -ErrorAction SilentlyContinue | ForEach-Object { Log "Start-App: killing leftover SubtitleTranslator pid=$($_.Id) path=$($_.Path)"; try { $_.Kill(); [void]$_.WaitForExit(3000) } catch {} }
    $args2 = @($extra)
    $t0 = [QaWin]::Now()
    $script:AppProc = if ($args2.Count) { Start-Process -FilePath $Qa.App -ArgumentList $args2 -WorkingDirectory $Qa.Dist -PassThru } else { Start-Process -FilePath $Qa.App -WorkingDirectory $Qa.Dist -PassThru }
    $deadline = [DateTime]::Now.AddMilliseconds($waitMs)
    while ([DateTime]::Now -lt $deadline) {
        $h = [QaWin]::Find([uint32]$script:AppProc.Id, '자막 번역', $false)
        if ($h -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 20
    }
    $startupMs = [QaWin]::Ms($t0, [QaWin]::Now())
    Start-Sleep -Milliseconds 700
    $script:Uia = $null
    return $startupMs
}
function Stop-App {
    if ($script:AppProc -and !$script:AppProc.HasExited) {
        $h = [QaWin]::Find([uint32]$script:AppProc.Id, '자막 번역', $false)
        if ($h -ne [IntPtr]::Zero) { [void][QaWin]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
        if (!$script:AppProc.WaitForExit(5000)) { $script:AppProc.Kill() }
    }
    Get-Process -Name SubtitleTranslator -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$($Qa.Dist)*" } | ForEach-Object { $_.Kill() }
    $script:AppProc = $null; $script:Uia = $null
}
function App-Alive { $script:AppProc -and !$script:AppProc.HasExited }
function Hotkey([string]$name) {
    switch ($name) {
        'T' { [QaWin]::Key(0x54, $true, $true) }  'P' { [QaWin]::Key(0x50, $true, $true) }
        'H' { [QaWin]::Key(0x48, $true, $true) }  'F8' { [QaWin]::Key(0x77, $true, $true) }
        'F9' { [QaWin]::Key(0x78, $true, $true) } 'Esc' { [QaWin]::Key(0x1B, $false, $false) }
    }
}
function Get-Hwnd([string]$title, [bool]$visible = $false) { if (!(App-Alive)) { return [IntPtr]::Zero }; [QaWin]::Find([uint32]$script:AppProc.Id, $title, $visible) }
function Get-Overlay { Get-Hwnd '화면 위 번역 자막' $true }
function Get-WinRect($h) { $r = New-Object QaWin+RECT; [void][QaWin]::GetWindowRect($h, [ref]$r); $r }

# ---------- UI Automation ----------
function Get-UiaMain {
    # Qt exposes the minimized main window (auto mode calls showMinimized) only reliably through the raw tree walker,
    # so walk instead of FindFirst, and re-validate the cached elements on every use.
    if ($script:Uia -and $script:Uia.Main -and $script:Uia.Pid -eq $script:AppProc.Id) { return $script:Uia }
    if (!(App-Alive)) { return $null }
    $AE = [System.Windows.Automation.AutomationElement]
    $pidCond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $script:AppProc.Id)
    $walker = [System.Windows.Automation.TreeWalker]::RawViewWalker
    foreach ($top in @($AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $pidCond))) {
        # Qt reports the minimized main window as 'SubtitleTranslator' instead of its title.
        try { if (@('자막 번역', 'SubtitleTranslator') -notcontains $top.Current.Name) { continue } } catch { continue }
        $edit = $null; $auto = $null; $all = New-Object System.Collections.ArrayList
        $stack = New-Object System.Collections.Stack; $stack.Push($top)
        while ($stack.Count) {
            $el = $stack.Pop(); $ch = $walker.GetFirstChild($el)
            while ($ch) {
                try {
                    $c = $ch.Current; [void]$all.Add($ch)
                    if (!$edit -and $c.ControlType -eq [System.Windows.Automation.ControlType]::Edit) { $edit = $ch }
                    if (!$auto -and $c.Name -and $c.Name.StartsWith('자동 번역') -and $c.ControlType -ne [System.Windows.Automation.ControlType]::Text) { $auto = $ch }
                } catch { }
                $stack.Push($ch); $ch = $walker.GetNextSibling($ch)
            }
        }
        $script:Uia = @{ Main = $top; Edit = $edit; Auto = $auto; All = $all; Pid = $script:AppProc.Id }
        return $script:Uia
    }
    return $null
}
function Get-OcrText {
    $u = Get-UiaMain; if (!$u -or !$u.Edit) { return $null }
    try {
        $vp = $null
        if ($u.Edit.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) { return $vp.Current.Value }
        $tp = $null
        if ($u.Edit.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$tp)) { return $tp.DocumentRange.GetText(-1) }
    } catch { $script:Uia = $null }
    return $null
}
function Get-UiTexts {
    $script:Uia = $null; $u = Get-UiaMain; if (!$u) { return @() }
    @($u.All | ForEach-Object { try { if ($_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Text) { $_.Current.Name } } catch { } })
}
function Find-UiButton([string]$prefix) {
    # Any control type: the checkable auto button is exposed as a check box / toggle, not a Button.
    $script:Uia = $null; $u = Get-UiaMain; if (!$u) { return $null }
    foreach ($b in $u.All) { try { if ($b.Current.ControlType -ne [System.Windows.Automation.ControlType]::Text -and $b.Current.Name -and $b.Current.Name.StartsWith($prefix)) { return $b } } catch { } }
    return $null
}
function Invoke-UiButton([string]$prefix) {
    $b = Find-UiButton $prefix; if (!$b) { return $false }
    $ip = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); return $true }
    return $false
}
function Get-AutoOn {
    $u = Get-UiaMain; $b = if ($u) { $u.Auto }; if (!$b) { return $null }
    $tp = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) { return ($tp.Current.ToggleState -eq [System.Windows.Automation.ToggleState]::On) }
    return $null
}
function Get-StatusText { (Get-UiTexts | Where-Object { $_ -match '준비|자동 번역|OCR|처리|번역|영역|단축키|취소|마우스|모니터' } | Select-Object -First 3) -join ' / ' }

# ---------- screen ----------
function Save-Screen($rect, $path) {
    try {
        $bmp = New-Object System.Drawing.Bitmap ([math]::Max(1, $rect.Right - $rect.Left)), ([math]::Max(1, $rect.Bottom - $rect.Top))
        $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size); $g.Dispose()
        $bmp.Save($path); $bmp.Dispose()
    } catch {}
}

# ---------- process metrics ----------
function Get-Metrics {
    if (!(App-Alive)) { return $null }
    $p = Get-Process -Id $script:AppProc.Id -ErrorAction SilentlyContinue; if (!$p) { return $null }
    [pscustomobject]@{
        t = [QaWin]::Now(); cpuMs = $p.TotalProcessorTime.TotalMilliseconds
        privateMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 2); workingMB = [math]::Round($p.WorkingSet64 / 1MB, 2)
        handles = $p.HandleCount; gdi = [QaWin]::GetGuiResources($p.Handle, 0); user = [QaWin]::GetGuiResources($p.Handle, 1)
        threads = $p.Threads.Count
    }
}
$Qa.Cores = [Environment]::ProcessorCount

# ---------- fake game ----------
$script:Game = $null
function Start-Game([hashtable]$scenario, [string]$name) {
    Stop-Game
    $scenario.events = Join-Path $Qa.Results "game-$name-events.csv"
    $file = Join-Path $Qa.Results "game-$name.json"
    ($scenario | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 $file
    $script:Game = Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSScriptRoot\fake-game.ps1`"", '-Scenario', "`"$file`"") -PassThru -WindowStyle Hidden
    $deadline = [DateTime]::Now.AddSeconds(15)
    while ([DateTime]::Now -lt $deadline -and !(Test-Path $scenario.events)) { Start-Sleep -Milliseconds 50 }
    Start-Sleep -Milliseconds 300
    return $scenario.events
}
function Stop-Game { if ($script:Game -and !$script:Game.HasExited) { $script:Game.Kill() }; $script:Game = $null }
function Read-GameEvents($path) { if (Test-Path $path) { @(Import-Csv $path) } else { @() } }

# Records OCR-text changes (UIA) and overlay visibility until $untilMs elapses.
function Watch-App([double]$durationMs, [int]$pollMs = 40) {
    $t0 = [QaWin]::Now(); $rows = @(); $lastText = '<none>'; $lastOverlay = $null
    while ([QaWin]::Ms($t0, [QaWin]::Now()) -lt $durationMs) {
        $text = Get-OcrText
        $ov = (Get-Overlay) -ne [IntPtr]::Zero
        if ($text -ne $lastText -or $ov -ne $lastOverlay) {
            $rows += [pscustomobject]@{ t = [QaWin]::Now(); text = $text; overlay = $ov }
            $lastText = $text; $lastOverlay = $ov
        }
        Start-Sleep -Milliseconds $pollMs
    }
    return $rows
}
function Norm([string]$s) { if ($null -eq $s) { return '' }; ($s -replace '\s', '') }
