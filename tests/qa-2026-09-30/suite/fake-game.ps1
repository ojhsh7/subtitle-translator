# Fake game dialogue window for the QA suite. Reads a JSON scenario and writes timestamped events.
param([string]$Scenario)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
if (-not ('GameClock' -as [type])) {
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class GameClock {
    [DllImport("kernel32.dll")] static extern void GetSystemTimePreciseAsFileTime(out long t);
    public static long Now() { long t; GetSystemTimePreciseAsFileTime(out t); return t; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
}
[void][GameClock]::SetProcessDPIAware()
$s = Get-Content -LiteralPath $Scenario -Raw -Encoding UTF8 | ConvertFrom-Json
function V($name, $default) { if ($null -ne $s.$name) { $s.$name } else { $default } }
$lines = @($s.lines)
$holdMs = V 'holdMs' 3000; $blankMs = V 'blankMs' 0; $typeMs = V 'typewriterMs' 0
$moving = [bool](V 'moving' $false); $loop = [bool](V 'loop' $false); $duration = V 'durationSec' 0
$style = V 'style' 'dark'; $blankStyle = V 'blankStyle' 'panel'; $fontSize = V 'fontSize' 32
$border = V 'border' 'none'

$writer = New-Object System.IO.StreamWriter($s.events, $false, [Text.Encoding]::UTF8)
$writer.AutoFlush = $true
$writer.WriteLine('t,type,index,text')
function Ev($type, $index, $text) { $writer.WriteLine("$([GameClock]::Now()),$type,$index,`"$($text -replace '"', '""')`"") }

$form = New-Object System.Windows.Forms.Form
$form.StartPosition = 'Manual'
$form.Location = New-Object System.Drawing.Point([int]$s.x, [int]$s.y)
$form.ClientSize = New-Object System.Drawing.Size([int]$s.w, [int]$s.h)
$form.FormBorderStyle = if ($border -eq 'window') { 'FixedSingle' } else { 'None' }
if ($border -eq 'maximized') { $form.WindowState = 'Maximized' }
$form.TopMost = $true
$form.ShowInTaskbar = $true
$form.Text = 'QA Fake Game'
$prop = $form.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'NonPublic,Instance'); $prop.SetValue($form, $true, $null)
$font = New-Object System.Drawing.Font('Yu Gothic UI', [single]$fontSize, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)

$state = @{ index = 0; phase = 'type'; since = [GameClock]::Now(); chars = 0; frame = 0; text = ''; start = [GameClock]::Now() }
if ($lines.Count -eq 0) { $state.phase = 'idle' }
$bgDark = [System.Drawing.Color]::FromArgb(28, 30, 44); $bgLight = [System.Drawing.Color]::FromArgb(236, 234, 226)

$form.Add_Paint({
    param($sender, $e)
    $g = $e.Graphics
    $bg = if ($style -eq 'light') { $bgLight } else { $bgDark }
    if ($state.phase -eq 'blank' -and $blankStyle -eq 'white') { $bg = [System.Drawing.Color]::White }
    $g.Clear($bg)
    if ($moving) {
        $off = ($state.frame * 6) % 80
        for ($x = -80; $x -lt $form.ClientSize.Width + 80; $x += 40) {
            $c = [System.Drawing.Color]::FromArgb(60 + (($x / 40) % 3) * 30, 70, 110)
            $brush = New-Object System.Drawing.SolidBrush $c
            $g.FillRectangle($brush, $x + $off, 0, 18, $form.ClientSize.Height)
            $brush.Dispose()
        }
    }
    if ($state.phase -eq 'blank' -and $blankStyle -eq 'frame') {
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(180, 160, 120)), 4
        $g.DrawRectangle($pen, 6, 6, $form.ClientSize.Width - 12, $form.ClientSize.Height - 12); $pen.Dispose()
    }
    if ($state.text) {
        $pt = New-Object System.Drawing.Point 24, ([int](($form.ClientSize.Height - $font.Height) / 2))
        $fg = if ($style -eq 'light') { [System.Drawing.Color]::Black } else { [System.Drawing.Color]::White }
        if ($style -ne 'light') {
            foreach ($d in @(@(-2, 0), @(2, 0), @(0, -2), @(0, 2), @(-1, -1), @(1, 1), @(-1, 1), @(1, -1))) {
                [System.Windows.Forms.TextRenderer]::DrawText($g, $state.text, $font, (New-Object System.Drawing.Point ($pt.X + $d[0]), ($pt.Y + $d[1])), [System.Drawing.Color]::Black, [System.Drawing.Color]::Transparent)
            }
        }
        [System.Windows.Forms.TextRenderer]::DrawText($g, $state.text, $font, $pt, $fg, [System.Drawing.Color]::Transparent)
    }
})

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 15
$timer.Add_Tick({
    $now = [GameClock]::Now()
    $elapsed = ($now - $state.since) / 10000.0
    $dirty = $false
    if ($moving -and (($now - $state.start) / 10000.0) -ge ($state.frame + 1) * 33) { $state.frame++; $dirty = $true }
    if ($duration -gt 0 -and ($now - $state.start) / 10000000.0 -ge $duration) { Ev 'end' $state.index ''; $form.Close(); return }
    switch ($state.phase) {
        'type' {
            $line = [string]$lines[$state.index]
            $want = if ($typeMs -gt 0) { [math]::Min($line.Length, [math]::Floor($elapsed / $typeMs) + 1) } else { $line.Length }
            if ($want -ne $state.chars) {
                $state.chars = $want; $state.text = $line.Substring(0, $want); $dirty = $true
                if ($want -eq 1 -and $typeMs -gt 0) { Ev 'start' $state.index $line }
                if ($want -eq $line.Length) { Ev 'complete' $state.index $line; $state.phase = 'hold'; $state.since = $now }
            }
        }
        'hold' {
            if ($elapsed -ge $holdMs) {
                if ($blankMs -gt 0) { $state.phase = 'blank'; $state.text = ''; $state.since = $now; $dirty = $true; Ev 'blank' $state.index '' }
                else { $state.phase = 'next' }
            }
        }
        'blank' { if ($elapsed -ge $blankMs) { $state.phase = 'next' } }
    }
    if ($state.phase -eq 'next') {
        $state.index++
        if ($state.index -ge $lines.Count) {
            if ($loop) { $state.index = 0 } else { Ev 'end' $state.index ''; $state.phase = 'idle' }
        }
        if ($state.phase -ne 'idle') { $state.phase = 'type'; $state.chars = 0; $state.since = $now; $state.text = '' }
    }
    if ($dirty) { $form.Invalidate() }
})
$form.Add_Shown({ Ev 'shown' -1 ''; $timer.Start() })
[System.Windows.Forms.Application]::Run($form)
$writer.Close()
