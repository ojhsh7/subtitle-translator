# C-06: 1 hour of auto translation (moving background + a new line every 5 s), metrics every 10 s.
# No key is loaded (OCR path only). UIA is not polled here so it cannot distort the numbers.
param([int]$Minutes = 60)
. "$PSScriptRoot\qa-common.ps1"
$script:LogFile = Join-Path $Qa.Results 'suite-leak.log'
$lines = @('Press any key to continue.', 'The door is locked.', 'Where did you find this map?', 'We should leave before sunset.',
    'Your inventory is full.', 'Do you want to save the game?', 'A new quest is available.', 'The bridge has collapsed.',
    'Talk to the merchant in town.', 'You gained a new skill.', 'Please wait a moment.', 'The weather is nice today.')
$rows = @()
try {
    Use-QaBaseSettings (Rect 40 1000 820 140)
    [void](Start-App)
    $ev = Start-Game @{ x = 40; y = 1000; w = 820; h = 140; lines = $lines; holdMs = 5000; loop = $true; moving = $true; durationSec = ($Minutes * 60 + 120) } 'c06'
    Hotkey 'P'
    Log "C-06 started for $Minutes min"
    $t0 = [QaWin]::Now()
    while ([QaWin]::Ms($t0, [QaWin]::Now()) -lt $Minutes * 60000) {
        $m = Get-Metrics
        if (!$m) { Log 'C-06 app exited unexpectedly'; break }
        $rows += [pscustomobject]@{ sec = [math]::Round([QaWin]::Ms($t0, $m.t) / 1000, 1); cpuMs = $m.cpuMs; privateMB = $m.privateMB; workingMB = $m.workingMB; handles = $m.handles; gdi = $m.gdi; user = $m.user; threads = $m.threads }
        if ($rows.Count % 30 -eq 0) { Save-Rows $rows 'C-06_metrics.csv'; Log "C-06 $([math]::Round($rows[-1].sec/60,1)) min private=$($m.privateMB)MB handles=$($m.handles) gdi=$($m.gdi)" }
        Start-Sleep -Seconds 10
    }
    Hotkey 'P'
    $alive = App-Alive
    $events = @(Read-GameEvents $ev | Where-Object type -eq 'complete').Count
    Log "C-06 done alive=$alive samples=$($rows.Count) linesShown=$events"
} finally {
    Save-Rows $rows 'C-06_metrics.csv'
    Stop-Game; Restore-UserSettings
}
