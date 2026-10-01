# App-screen cases (A, C, D) from plan.md. No DeepL calls except A-04 and T-10 (-WithKeyCases).
# Usage: suite-app.ps1 [-Cases A-01,A-03] [-Dist D:\...\dist] [-Tag current] [-WithKeyCases]
param([string[]]$Cases = @(), [string]$Dist = '', [string]$Tag = 'current', [switch]$WithKeyCases, [switch]$NoRestore)
. "$PSScriptRoot\qa-common.ps1"
# powershell -File passes "A-01,C-01" as one string; split it.
$Cases = @($Cases | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Dist) { $Qa.Dist = $Dist; $Qa.App = "$Dist\SubtitleTranslator.exe" }
$Qa.Results = Join-Path $Qa.Results $Tag; New-Item -ItemType Directory -Force -Path $Qa.Results | Out-Null
$script:LogFile = Join-Path $Qa.Results 'suite-app.log'
$summary = @()
function Want($id) { $Cases.Count -eq 0 -or $Cases -contains $id }
function Record($id, $result, $detail, $tag = '[측정함]') {
    $script:summary += [pscustomobject]@{ case = $id; result = $result; detail = $detail; tag = $tag }
    Log "$id $result | $detail"
}
function Sim([string]$a, [string]$b) {
    $a = Norm $a; $b = Norm $b
    if ($a.Length -eq 0 -and $b.Length -eq 0) { return 1.0 }
    # Levenshtein distance on a flat array (PS 5.1 cannot parse 2-D indexes inside method arguments).
    $w = $b.Length + 1
    $d = New-Object 'int[]' (($a.Length + 1) * $w)
    for ($i = 0; $i -le $a.Length; $i++) { $d[$i * $w] = $i }
    for ($j = 0; $j -le $b.Length; $j++) { $d[$j] = $j }
    for ($i = 1; $i -le $a.Length; $i++) {
        for ($j = 1; $j -le $b.Length; $j++) {
            $cost = 1
            if ($a[$i - 1] -eq $b[$j - 1]) { $cost = 0 }
            $del = $d[($i - 1) * $w + $j] + 1
            $ins = $d[$i * $w + $j - 1] + 1
            $sub = $d[($i - 1) * $w + $j - 1] + $cost
            $best = $del
            if ($ins -lt $best) { $best = $ins }
            if ($sub -lt $best) { $best = $sub }
            $d[$i * $w + $j] = $best
        }
    }
    1.0 - $d[$a.Length * $w + $b.Length] / [math]::Max($a.Length, $b.Length)
}

# Standard layout on a 2560x1440 primary screen (logical = physical at 100%).
$G = @{ x = 40; y = 1000; w = 820; h = 140 }
$captureRegion = Rect $G.x $G.y $G.w $G.h
$Lines20 = @('Press any key to continue.', 'The door is locked.', 'Where did you find this map?', 'We should leave before sunset.',
    'Your inventory is full.', 'Do you want to save the game?', 'A new quest is available.', 'The bridge has collapsed.',
    'Talk to the merchant in town.', 'You gained a new skill.', 'Please wait a moment.', 'The weather is nice today.',
    'Can you hear that sound?', 'Follow me to the castle.', 'Your health is low.', 'Welcome back, traveler.',
    'This sword looks ancient.', 'The shop is closed now.', 'I will meet you there.', 'Thank you for your help.')

# Waits until the overlay is hidden then visible again; returns ms from $t0 or $null.
function Wait-OverlayCycle($t0, [int]$timeoutMs = 8000) {
    $sawHidden = $false
    while ([QaWin]::Ms($t0, [QaWin]::Now()) -lt $timeoutMs) {
        $visible = (Get-Overlay) -ne [IntPtr]::Zero
        if (!$visible) { $sawHidden = $true }
        elseif ($sawHidden) { return [QaWin]::Ms($t0, [QaWin]::Now()) }
        Start-Sleep -Milliseconds 10
    }
    return $null
}
# OCR cycles from a Watch-App log: a non-empty text followed by '' means a new OCR started (autoTick clears first).
function Get-Cycles($rows) { $n = 0; $prev = $null; foreach ($r in $rows) { if ($prev -and $r.text -eq '') { $n++ }; $prev = $r.text }; $n }

try {
    Log "suite-app start tag=$Tag dist=$($Qa.Dist)"
    if (!(Test-Path $Qa.App)) { throw "app missing: $($Qa.App)" }
    Get-Process -Name SubtitleTranslator -ErrorAction SilentlyContinue | ForEach-Object { Log "closing other SubtitleTranslator pid=$($_.Id)"; $_.CloseMainWindow() | Out-Null; if (!$_.WaitForExit(3000)) { $_.Kill() } }

    # ---------------- A-01 / C-01: Ctrl+Alt+T -> overlay ----------------
    if ((Want 'A-01') -or (Want 'C-01')) { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Press any key to continue.'); holdMs = 3600000 } 'a01' | Out-Null
        [void](Start-App)
        $rows = @()
        for ($i = 1; $i -le 20; $i++) {
            $t0 = [QaWin]::Now(); Hotkey 'T'
            $ms = Wait-OverlayCycle $t0 8000
            Start-Sleep -Milliseconds 300
            $text = Get-OcrText
            $rows += [pscustomobject]@{ run = $i; overlayMs = $ms; text = $text; sim = [math]::Round((Sim $text 'Press any key to continue.'), 3); alive = (App-Alive) }
            Start-Sleep -Milliseconds 1200
        }
        Save-Rows $rows 'A-01_C-01.csv'
        $st = Stat ($rows | ForEach-Object { $_.overlayMs })
        $ok = @($rows | Where-Object { $_.overlayMs -ne $null -and $_.overlayMs -le 2000 -and $_.sim -ge 0.9 }).Count
        Record 'A-01' $(if ($ok -eq 20) { 'PASS' } else { 'FAIL' }) "overlay<=2s and text ok: $ok/20"
        Record 'C-01' 'MEASURED' "Ctrl+Alt+T->overlay ms median=$($st.median) p95=$($st.p95) max=$($st.max) n=$($st.n) (±10ms poll, no translation)"
    } catch { Record 'A-01' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-02: toggle auto 20x ----------------
    if (Want 'A-02') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('The door is locked.'); holdMs = 3600000 } 'a02' | Out-Null
        [void](Start-App)
        for ($i = 1; $i -le 20; $i++) { Hotkey 'P'; Start-Sleep -Milliseconds 500 }
        Start-Sleep -Milliseconds 800
        $on = Get-AutoOn; $ov = (Get-Overlay) -ne [IntPtr]::Zero
        Record 'A-02' $(if ((App-Alive) -and $on -eq $false -and !$ov) { 'PASS' } else { 'FAIL' }) "after 20 toggles: alive=$(App-Alive) autoOn=$on overlayVisible=$ov"
    } catch { Record 'A-02' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-03 / C-02 / C-04(auto): dialogue changes ----------------
    if ((Want 'A-03') -or (Want 'C-02')) { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App)
        $m0 = Get-Metrics
        $ev = Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = $Lines20; holdMs = 3000; startDelayMs = 0 } 'a03'
        Hotkey 'P'
        $watch = Watch-App (20 * 3000 + 5000) 40
        $m1 = Get-Metrics
        Hotkey 'P'
        $events = @(Read-GameEvents $ev | Where-Object type -eq 'complete')
        $rows = @()
        for ($i = 0; $i -lt $events.Count; $i++) {
            $t = [int64]$events[$i].t
            $limit = if ($i + 1 -lt $events.Count) { [int64]$events[$i + 1].t + 15000000 } else { $t + 60000000 }
            $hit = $watch | Where-Object { $_.t -ge $t -and $_.t -le $limit -and (Sim $_.text $events[$i].text) -ge 0.8 } | Select-Object -First 1
            $rows += [pscustomobject]@{ index = $i; line = $events[$i].text; latencyMs = $(if ($hit) { [math]::Round([QaWin]::Ms($t, $hit.t), 0) }); read = $(if ($hit) { $hit.text }) }
        }
        Save-Rows $rows 'A-03_C-02.csv'; Save-Rows $watch 'A-03_watch.csv'
        $missed = @($rows | Where-Object { $_.latencyMs -eq $null }).Count
        $cycles = Get-Cycles $watch
        $st = Stat ($rows | ForEach-Object { $_.latencyMs })
        Record 'A-03' $(if ($missed -eq 0 -and $cycles -le $events.Count + 1) { 'PASS' } else { 'FAIL' }) "lines=$($events.Count) missed=$missed ocrCycles~$cycles"
        Record 'C-02' 'MEASURED' "line complete -> OCR text ms median=$($st.median) p95=$($st.p95) max=$($st.max) (UIA poll 40ms)"
        if ($m0 -and $m1) { Record 'C-04a' 'MEASURED' "auto 20 lines: private $($m0.privateMB)->$($m1.privateMB)MB, working $($m0.workingMB)->$($m1.workingMB)MB, handles $($m0.handles)->$($m1.handles), gdi $($m0.gdi)->$($m1.gdi)" }
    } catch { Record 'A-03' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-05: blank dialogue box (D-01 on the app path) ----------------
    if (Want 'A-05') { foreach ($bs in 'panel', 'white', 'frame') { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App)
        $lines = @('Your inventory is full.', 'A new quest is available.', 'Please wait a moment.', 'Welcome back, traveler.')
        $ev = Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = $lines; holdMs = 3000; blankMs = 4000; blankStyle = $bs } "a05-$bs"
        Hotkey 'P'
        $watch = Watch-App (4 * 7000 + 3000) 40
        Hotkey 'P'
        $events = @(Read-GameEvents $ev)
        $junk = @(); $overlayDuringBlank = 0
        foreach ($b in ($events | Where-Object type -eq 'blank')) {
            $t = [int64]$b.t; $end = $t + 40000000
            $inBlank = $watch | Where-Object { $_.t -ge ($t + 1500000) -and $_.t -le $end }
            foreach ($w in $inBlank) {
                if ($w.text -and ($lines | Where-Object { (Sim $w.text $_) -ge 0.8 }).Count -eq 0) { $junk += $w.text }
            }
            $last = $watch | Where-Object { $_.t -le $end } | Select-Object -Last 1
            if ($last -and $last.overlay) { $overlayDuringBlank++ }
        }
        Save-Rows $watch "A-05_$bs.csv"
        $j = ($junk | Select-Object -Unique) -join ' | '
        Record "A-05-$bs" $(if ($junk.Count -eq 0 -and $overlayDuringBlank -eq 0) { 'PASS' } else { 'FAIL' }) "junk OCR during blank=[$j] overlayStillVisibleAtBlankEnd=$overlayDuringBlank/4"
    } catch { Record "A-05-$bs" 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } } }

    # ---------------- A-06: region selection ----------------
    if (Want 'A-06') { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App)
        function Region { (Get-ItemProperty "$($Qa.RegPath)\capture" -ErrorAction SilentlyContinue).region }
        function Select-By([scriptblock]$action) { if (!(Invoke-UiButton '자막 영역 선택')) { return 'no-button' }; Start-Sleep -Milliseconds 900; & $action; Start-Sleep -Milliseconds 900; Region }
        $res = [ordered]@{}
        $res.normal = Select-By { [QaWin]::Drag(300, 300, 900, 500) }
        $before = Region
        $res.esc = Select-By { Hotkey 'Esc' }; $res.escUnchanged = ($res.esc -eq $before)
        $res.tiny10 = Select-By { [QaWin]::Drag(400, 400, 410, 410) }
        $before = Region
        $res.tiny7 = Select-By { [QaWin]::Drag(400, 400, 406, 406); Start-Sleep -Milliseconds 400; Hotkey 'Esc' }; $res.tiny7Unchanged = ($res.tiny7 -eq $before)
        $res.full = Select-By { [QaWin]::Drag(0, 0, 2559, 1439) }
        Stop-App; Start-Sleep -Milliseconds 500; [void](Start-App)
        $res.restored = [bool](Get-UiTexts | Where-Object { $_ -like '*저장된 영역*' })
        $detail = ($res.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join '; '
        $pass = $res.normal -like '@Rect(300 300 *' -and $res.escUnchanged -and $res.tiny10 -like '@Rect(400 400 1* 1*)' -and $res.tiny7Unchanged -and $res.full -like '@Rect(0 0 25*' -and $res.restored
        Record 'A-06' $(if ($pass) { 'PASS' } else { 'FAIL' }) $detail
    } catch { Record 'A-06' 'ERROR' $_.Exception.Message } finally { Stop-App } }

    # Which mouse/hover keys did the app register? (this PC: Ctrl+Alt+M is taken -> F8)
    function Get-KeyLabels { $t = (Get-UiTexts) -join ' '; @{ mouseF8 = $t -like '*Ctrl+Alt+F8*'; hoverF9 = $t -like '*Ctrl+Alt+F9*' } }

    # ---------------- A-07: mouse-area translation ----------------
    if (Want 'A-07') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Talk to the merchant in town.'); holdMs = 3600000 } 'a07' | Out-Null
        [void](Start-App); $k = Get-KeyLabels
        if (!$k.mouseF8) { Record 'A-07' 'SKIP' 'mouse key is Ctrl+Alt+M on this run; not sent (another app owns it on this PC)' '[환경 부족으로 미검증]' }
        else {
            $rows = @()
            for ($i = 1; $i -le 10; $i++) {
                [void][QaWin]::SetCursorPos(360, 1070); Start-Sleep -Milliseconds 200
                $t0 = [QaWin]::Now(); Hotkey 'F8'
                $ms = Wait-OverlayCycle $t0 8000; Start-Sleep -Milliseconds 300
                $ov = Get-Overlay; $r = if ($ov -ne [IntPtr]::Zero) { Get-WinRect $ov }
                $rows += [pscustomobject]@{ run = $i; overlayMs = $ms; text = (Get-OcrText); overlayTop = $r.Top; overlayBottom = $r.Bottom; autoOn = (Get-AutoOn) }
                Start-Sleep -Milliseconds 800
            }
            Save-Rows $rows 'A-07.csv'
            $ok = @($rows | Where-Object { $_.overlayMs -ne $null -and (Sim $_.text 'Talk to the merchant in town.') -ge 0.8 }).Count
            $st = Stat ($rows | ForEach-Object { $_.overlayMs })
            Record 'A-07' $(if ($ok -eq 10) { 'PASS' } else { 'FAIL' }) "read+overlay $ok/10, F8->overlay median=$($st.median)ms p95=$($st.p95)"
        }
    } catch { Record 'A-07' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-08: hover ----------------
    if (Want 'A-08') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('This sword looks ancient.'); holdMs = 3600000 } 'a08' | Out-Null
        [void](Start-App); $k = Get-KeyLabels
        $hk = if ($k.hoverF9) { 'F9' } else { 'H' }
        [void][QaWin]::SetCursorPos(1500, 400); Hotkey $hk; Start-Sleep -Milliseconds 600
        [void][QaWin]::SetCursorPos(330, 1070)
        $w1 = Watch-App 2500 40            # dwell 1s -> one read
        $w2 = Watch-App 3000 40            # stay -> no new read
        [void][QaWin]::SetCursorPos(560, 1070)
        $w3 = Watch-App 2500 40            # moved -> read again
        Hotkey $hk
        $read1 = [bool]($w1 | Where-Object { (Sim $_.text 'This sword looks ancient.') -ge 0.8 })
        $repeat = @($w2 | Where-Object { $_.text -eq '' }).Count
        $read3 = @($w3 | Where-Object { $_.text -eq '' }).Count -ge 1 -or [bool]($w3 | Where-Object { $_.text })
        Record 'A-08' $(if ($read1 -and $repeat -eq 0 -and $read3) { 'PASS' } else { 'FAIL' }) "key=Ctrl+Alt+$hk dwellRead=$read1 repeatsWhileStill=$repeat readAfterMove=$read3"
    } catch { Record 'A-08' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-09: feature switching ----------------
    if (Want 'A-09') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Follow me to the castle.'); holdMs = 3600000 } 'a09' | Out-Null
        [void](Start-App); $k = Get-KeyLabels
        Hotkey 'P'; Start-Sleep -Milliseconds 1500; $on1 = Get-AutoOn
        $afterMouse = $null
        if ($k.mouseF8) { [void][QaWin]::SetCursorPos(360, 1070); Hotkey 'F8'; Start-Sleep -Milliseconds 2500; $afterMouse = Get-AutoOn }
        Hotkey 'P'; Start-Sleep -Milliseconds 1500; $on2 = Get-AutoOn
        $hk = if ($k.hoverF9) { 'F9' } else { 'H' }
        [void][QaWin]::SetCursorPos(1500, 400); Hotkey $hk; Start-Sleep -Milliseconds 1500; $afterHover = Get-AutoOn
        Hotkey $hk
        $pass = $on1 -and ($afterMouse -eq $false -or $afterMouse -eq $null) -and $on2 -and ($afterHover -eq $false)
        Record 'A-09' $(if ($pass) { 'PASS' } else { 'FAIL' }) "autoOn=$on1 afterMouse=$afterMouse autoOnAgain=$on2 afterHover=$afterHover"
    } catch { Record 'A-09' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-10: overlay placement ----------------
    if (Want 'A-10') { try {
        $cases10 = @(@{ name = 'middle'; y = 1000; expect = 'below' }, @{ name = 'bottom'; y = 1240; expect = 'above' })
        $details = @(); $pass = $true
        foreach ($c in $cases10) {
            Use-QaBaseSettings (Rect $G.x $c.y $G.w $G.h)
            Start-Game @{ x = $G.x; y = $c.y; w = $G.w; h = $G.h; lines = @('Can you hear that sound?'); holdMs = 3600000 } "a10-$($c.name)" | Out-Null
            [void](Start-App)
            $t0 = [QaWin]::Now(); Hotkey 'T'; [void](Wait-OverlayCycle $t0 8000); Start-Sleep -Milliseconds 300
            $ov = Get-Overlay
            if ($ov -eq [IntPtr]::Zero) { $pass = $false; $details += "$($c.name): no overlay"; Stop-App; Stop-Game; continue }
            $r = Get-WinRect $ov
            $ex = [int64][QaWin]::GetWindowLongPtr($ov, -20)
            $pos = if ($r.Top -ge $c.y + $G.h) { 'below' } elseif ($r.Bottom -le $c.y) { 'above' } else { 'overlaps' }
            $lines = [math]::Round(($r.Bottom - $r.Top - 10) / 48.0, 1)
            $transparent = ($ex -band 0x20) -ne 0; $noActivate = ($ex -band 0x08000000) -ne 0
            if ($pos -ne $c.expect -or !$transparent -or !$noActivate -or ($r.Bottom - $r.Top) -gt 4 * 60 + 10) { $pass = $false }
            $details += "$($c.name): $pos (expect $($c.expect)) rect=($($r.Left),$($r.Top),$($r.Right),$($r.Bottom)) transparent=$transparent noActivate=$noActivate approxLines=$lines"
            Save-Screen $r (Join-Path $Qa.Results "A-10-$($c.name)-overlay.png")
            Stop-App; Stop-Game
        }
        Record 'A-10' $(if ($pass) { 'PASS' } else { 'FAIL' }) ($details -join ' || ')
    } catch { Record 'A-10' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-11: language change re-reads (UIA combo, may be unsupported) ----------------
    if (Want 'A-11') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Your health is low.'); holdMs = 3600000 } 'a11' | Out-Null
        [void](Start-App)
        Hotkey 'P'; Start-Sleep -Milliseconds 3000
        $u = Get-UiaMain
        $comboCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ComboBox)
        $combos = @($u.Main.FindAll([System.Windows.Automation.TreeScope]::Descendants, $comboCond))
        $changed = $false
        if ($combos.Count -ge 2) {
            $cb = $combos[1]; $ec = $null
            if ($cb.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$ec)) {
                $ec.Expand(); Start-Sleep -Milliseconds 500
                $itemCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, '영어')
                $item = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $itemCond)
                $sel = $null
                if ($item -and $item.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$sel)) { $sel.Select(); $changed = $true }
                try { $ec.Collapse() } catch {}
            }
        }
        if (!$changed) { Record 'A-11' 'SKIP' 'UI Automation could not select the language combo item' '[환경 부족으로 미검증]' }
        else {
            $w = Watch-App 5000 40
            $reread = (Get-Cycles $w) -ge 1 -or [bool]($w | Where-Object { $_.text -eq '' })
            $lang = (Get-ItemProperty "$($Qa.RegPath)\ocr").languages
            Record 'A-11' $(if ($reread -and $lang -eq 'eng') { 'PASS' } else { 'FAIL' }) "languageSaved=$lang rereadWithin5s=$reread"
        }
        Hotkey 'P'
    } catch { Record 'A-11' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- A-12: --auto ----------------
    if (Want 'A-12') { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App @('--auto')); Start-Sleep -Milliseconds 2000
        $h = Get-Hwnd '자막 번역'; $iconic = [QaWin]::IsIconic($h); $on = Get-AutoOn
        Record 'A-12' $(if ($on -and $iconic) { 'PASS' } else { 'FAIL' }) "with saved region: autoOn=$on minimized=$iconic"
    } catch { Record 'A-12' 'ERROR' $_.Exception.Message } finally { Stop-App } }

    # ---------------- C-03: idle CPU with auto on ----------------
    if (Want 'C-03') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('The shop is closed now.'); holdMs = 3600000 } 'c03' | Out-Null
        [void](Start-App); Hotkey 'P'; Start-Sleep -Seconds 30
        $m0 = Get-Metrics; $samples = @()
        for ($i = 0; $i -lt 30; $i++) { Start-Sleep -Seconds 10; $samples += Get-Metrics }
        $m1 = Get-Metrics
        $cpu = ($m1.cpuMs - $m0.cpuMs) / [QaWin]::Ms($m0.t, $m1.t) / $Qa.Cores * 100
        Save-Rows $samples 'C-03_samples.csv'
        Record 'C-03' $(if ($cpu -le 3) { 'PASS' } else { 'FAIL' }) "idle auto CPU=$([math]::Round($cpu,2))% of $($Qa.Cores) logical cores over 5 min (target <=3%), private $($m0.privateMB)->$($m1.privateMB)MB"
        Hotkey 'P'
    } catch { Record 'C-03' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- C-04: memory idle / multi ----------------
    if (Want 'C-04') { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App); Start-Sleep -Seconds 5
        $idle = @(); for ($i = 0; $i -lt 30; $i++) { $idle += Get-Metrics; Start-Sleep -Seconds 1 }
        Stop-App
        Use-QaBaseSettings $captureRegion $false 'multi'
        [void](Start-App)
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = $Lines20[0..9]; holdMs = 3000 } 'c04' | Out-Null
        Hotkey 'P'
        $multi = @(); for ($i = 0; $i -lt 35; $i++) { $multi += Get-Metrics; Start-Sleep -Seconds 1 }
        Hotkey 'P'
        Save-Rows ($idle + $multi) 'C-04_samples.csv'
        $iw = ($idle | Measure-Object workingMB -Maximum).Maximum; $ip = ($idle | Measure-Object privateMB -Maximum).Maximum
        $mw = ($multi | Measure-Object workingMB -Maximum).Maximum; $mp = ($multi | Measure-Object privateMB -Maximum).Maximum
        Record 'C-04' 'MEASURED' "idle max working=$iw MB private=$ip MB; multi(13 langs) auto max working=$mw MB private=$mp MB (doc ~380MB = benchmark host)"
    } catch { Record 'C-04' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- C-05: startup ----------------
    if (Want 'C-05') { try {
        Use-QaBaseSettings $captureRegion
        $times = @(); for ($i = 0; $i -lt 10; $i++) { $times += (Start-App); Stop-App; Start-Sleep -Milliseconds 700 }
        $st = Stat $times
        Record 'C-05' 'MEASURED' "launch->main window ms median=$($st.median) p95=$($st.p95) max=$($st.max) (warm file cache; reboot case = manual)"
    } catch { Record 'C-05' 'ERROR' $_.Exception.Message } finally { Stop-App } }

    # ---------------- C-07: typewriter ----------------
    if (Want 'C-07') { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App)
        $tw = @('The ancient gate opens only at midnight.', 'Bring me three herbs from the east forest.', 'Nobody has returned from that cave yet.', 'Keep your torch lit when you go below.', 'The king wants to see you right away now.')
        $ev = Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = $tw; holdMs = 2500; typewriterMs = 100; fontSize = 26 } 'c07'
        Hotkey 'P'
        $watch = Watch-App (5 * 6600 + 3000) 40
        Hotkey 'P'
        $events = @(Read-GameEvents $ev | Where-Object type -eq 'start')
        $rows = @()
        for ($i = 0; $i -lt $events.Count; $i++) {
            $t = [int64]$events[$i].t; $end = if ($i + 1 -lt $events.Count) { [int64]$events[$i + 1].t } else { $t + 70000000 }
            $seg = @($watch | Where-Object { $_.t -ge $t -and $_.t -lt $end })
            $texts = @($seg | Where-Object { $_.text } | ForEach-Object { $_.text })
            $partial = @($texts | Where-Object { (Sim $_ $tw[$i]) -lt 0.8 }).Count
            $rows += [pscustomobject]@{ index = $i; ocrCycles = (Get-Cycles $seg); partialReads = $partial; finalRead = [bool]($texts | Where-Object { (Sim $_ $tw[$i]) -ge 0.8 }) }
        }
        Save-Rows $rows 'C-07.csv'; Save-Rows $watch 'C-07_watch.csv'
        $part = ($rows | Measure-Object partialReads -Sum).Sum; $cyc = ($rows | Measure-Object ocrCycles -Sum).Sum
        Record 'C-07' 'MEASURED' "5 lines typed at 100ms/char (~4s each): OCR cycles=$cyc, partial-sentence reads=$part, final read ok=$(@($rows | Where-Object finalRead).Count)/5"
    } catch { Record 'C-07' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- C-08: moving background ----------------
    if (Want 'C-08') { try {
        Use-QaBaseSettings $captureRegion
        [void](Start-App)
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('We should leave before sunset.'); holdMs = 3600000; moving = $true } 'c08' | Out-Null
        Hotkey 'P'; Start-Sleep -Seconds 5
        $m0 = Get-Metrics
        $watch = Watch-App 120000 60
        $m1 = Get-Metrics
        Hotkey 'P'
        $cpu = ($m1.cpuMs - $m0.cpuMs) / [QaWin]::Ms($m0.t, $m1.t) / $Qa.Cores * 100
        Save-Rows $watch 'C-08_watch.csv'
        Record 'C-08' 'MEASURED' "static line on moving background, 2 min: OCR cycles=$(Get-Cycles $watch) (design: ~1 per 3 s = 40), app CPU=$([math]::Round($cpu,2))% (includes UIA polling)"
    } catch { Record 'C-08' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- C-09: hover cancel then re-read latency ----------------
    if (Want 'C-09') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('I will meet you there.'); holdMs = 3600000 } 'c09' | Out-Null
        [void](Start-App); $k = Get-KeyLabels; $hk = if ($k.hoverF9) { 'F9' } else { 'H' }
        [void][QaWin]::SetCursorPos(1500, 400); Hotkey $hk; Start-Sleep -Milliseconds 600
        $rows = @()
        for ($r = 1; $r -le 5; $r++) {
            for ($i = 0; $i -lt 10; $i++) { [void][QaWin]::SetCursorPos(250 + ($i % 2) * 60 + $r * 20, 1070); Start-Sleep -Milliseconds 300 }
            $t0 = [QaWin]::Now()
            $w = Watch-App 8000 30
            $first = $w | Where-Object { $_.t -gt $t0 -and (Sim $_.text 'I will meet you there.') -ge 0.8 } | Select-Object -First 1
            $rows += [pscustomobject]@{ round = $r; msAfterStop = $(if ($first) { [math]::Round([QaWin]::Ms($t0, $first.t), 0) }) ; status = (Get-StatusText) }
        }
        Hotkey $hk
        Save-Rows $rows 'C-09.csv'
        $st = Stat ($rows | ForEach-Object { $_.msAfterStop })
        $timeouts = @($rows | Where-Object { $_.status -like '*20초*' }).Count
        Record 'C-09' 'MEASURED' "after 10 moves then stop: ms to read median=$($st.median) p95=$($st.p95) max=$($st.max) (includes 1s dwell) missing=$(5 - $st.n) timeoutMessages=$timeouts"
    } catch { Record 'C-09' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- D-01: hotkeys already taken ----------------
    if (Want 'D-01') { try {
        Use-QaBaseSettings $captureRegion
        $helper = Start-Process powershell.exe -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSScriptRoot\hold-hotkeys.ps1`"") -PassThru -WindowStyle Hidden
        Start-Sleep -Seconds 3
        [void](Start-App); Start-Sleep -Milliseconds 800
        $t = (Get-UiTexts) -join ' '
        $checks = [ordered]@{ T = $t -like '*Ctrl+Alt+T 등록 실패*'; P = $t -like '*Ctrl+Alt+P 등록 실패*'; M = $t -like '*M/F8 등록 실패*'; H = $t -like '*올려두기 단축키 등록 실패*' }
        if (!$helper.HasExited) { $helper.Kill() }
        Record 'D-01' $(if (@($checks.Values | Where-Object { !$_ }).Count -eq 0) { 'PASS' } else { 'FAIL' }) (($checks.GetEnumerator() | ForEach-Object { "$($_.Key)msg=$($_.Value)" }) -join ' ')
    } catch { Record 'D-01' 'ERROR' $_.Exception.Message } finally { Stop-App } }

    # ---------------- D-02: 5 presses per second ----------------
    if (Want 'D-02') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Thank you for your help.'); holdMs = 3600000 } 'd02' | Out-Null
        [void](Start-App); $m0 = Get-Metrics
        for ($i = 0; $i -lt 50; $i++) { Hotkey 'T'; Start-Sleep -Milliseconds 200 }
        Start-Sleep -Seconds 4
        $text = Get-OcrText; $m1 = Get-Metrics
        Record 'D-02' $(if ((App-Alive) -and (Sim $text 'Thank you for your help.') -ge 0.8) { 'PASS' } else { 'FAIL' }) "alive=$(App-Alive) finalText=[$text] handles $($m0.handles)->$($m1.handles)"
    } catch { Record 'D-02' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- D-03: close during OCR ----------------
    if (Want 'D-03') { try {
        Use-QaBaseSettings $captureRegion
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('The bridge has collapsed.'); holdMs = 3600000 } 'd03' | Out-Null
        $rows = @()
        for ($i = 1; $i -le 10; $i++) {
            [void](Start-App); $p = $script:AppProc
            Hotkey 'T'; Start-Sleep -Milliseconds 330
            $h = Get-Hwnd '자막 번역'
            $t0 = [QaWin]::Now(); [void][QaWin]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
            $exited = $p.WaitForExit(5000); $ms = [QaWin]::Ms($t0, [QaWin]::Now())
            $rows += [pscustomobject]@{ run = $i; exited = $exited; exitMs = [math]::Round($ms, 0); exitCode = $(if ($exited) { $p.ExitCode }) }
            if (!$exited) { $p.Kill() }
            $script:AppProc = $null
        }
        Save-Rows $rows 'D-03.csv'
        $bad = @($rows | Where-Object { !$_.exited -or $_.exitMs -gt 2000 -or $_.exitCode -ne 0 }).Count
        $st = Stat ($rows | ForEach-Object { $_.exitMs })
        Record 'D-03' $(if ($bad -eq 0) { 'PASS' } else { 'FAIL' }) "close during OCR: exit ms median=$($st.median) max=$($st.max), bad=$bad/10"
    } catch { Record 'D-03' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- D-04: framed window game ----------------
    if (Want 'D-04') { try {
        # Region covers title bar + client but stays inside the framed window (the old, larger region captured the desktop around it).
        Use-QaBaseSettings (Rect ($G.x + 2) ($G.y + 2) ($G.w) ($G.h + 30))
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Welcome back, traveler.'); holdMs = 3600000; border = 'window' } 'd04' | Out-Null
        [void](Start-App)
        $t0 = [QaWin]::Now(); Hotkey 'T'; $ms = Wait-OverlayCycle $t0 8000; Start-Sleep -Milliseconds 300
        $text = Get-OcrText
        Record 'D-04' $(if ((Sim $text 'Welcome back, traveler.') -ge 0.8) { 'PASS' } else { 'FAIL' }) "framed window: text=[$text] overlayMs=$ms"
    } catch { Record 'D-04' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    # ---------------- key cases (real DeepL, approved) ----------------
    # Status lines the app shows about translation (all UI texts, not only the first three).
    function Get-TransStatus { ((Get-UiTexts) | Where-Object { $_ -match '번역|DeepL|키|실패|완료|빈 공간|멈췄' -and $_ -notmatch '^설정 →|자막 글자 부분만' } | ForEach-Object { $_ -replace '\r?\n', ' / ' }) -join ' | ' }

    if ($WithKeyCases -and (Want 'T-10')) { try {
        Use-QaBaseSettings $captureRegion $true
        Start-Game @{ x = $G.x; y = $G.y; w = $G.w; h = $G.h; lines = @('Press any key to continue.'); holdMs = 3600000 } 't10' | Out-Null
        [void](Start-App); Start-Sleep -Milliseconds 500
        Log "T-10 before: status=[$(Get-TransStatus)]"
        $t0 = [QaWin]::Now(); Hotkey 'T'
        # Poll: overlay visible, result window visible, status text. Translation result appears after the network round trip.
        $trace = @(); $firstOverlay = $null; $lastStatus = ''
        while ([QaWin]::Ms($t0, [QaWin]::Now()) -lt 20000) {
            $ov = (Get-Overlay) -ne [IntPtr]::Zero; $rw = (Get-Hwnd '자막 번역 결과' $true) -ne [IntPtr]::Zero
            $st = Get-TransStatus
            if ($st -ne $lastStatus -or ($trace.Count -and ($trace[-1].overlay -ne $ov -or $trace[-1].resultWindow -ne $rw)) -or !$trace.Count) {
                $trace += [pscustomobject]@{ ms = [math]::Round([QaWin]::Ms($t0, [QaWin]::Now()), 0); overlay = $ov; resultWindow = $rw; status = $st }
                $lastStatus = $st
            }
            if ($ov -and !$firstOverlay -and $st -match '번역 완료|DeepL|번역했|완료') { $firstOverlay = [QaWin]::Ms($t0, [QaWin]::Now()) }
            Start-Sleep -Milliseconds 200
        }
        Save-Rows $trace 'T-10_trace.csv'
        $ovh = Get-Overlay; if ($ovh -ne [IntPtr]::Zero) { Save-Screen (Get-WinRect $ovh) (Join-Path $Qa.Results 'T-10-overlay.png') }
        $rwh = Get-Hwnd '자막 번역 결과' $true; if ($rwh -ne [IntPtr]::Zero) { Save-Screen (Get-WinRect $rwh) (Join-Path $Qa.Results 'T-10-resultwindow.png') }
        $final = $trace[-1]
        $sent = [bool]($trace | Where-Object { $_.status -match '번역 완료|DeepL이|번역 실패|인증' })
        if ($sent) { Add-DeeplCall 'T-10' 26 "app T, final=[$($final.status)]" }
        Record 'T-10' $(if ($final.overlay -and $final.status -match '완료' -and $final.status -notmatch '실패|없어') { 'CHECK-SCREENSHOT' } else { 'FAIL' }) "overlayFinal=$($final.overlay) resultWindow=$($final.resultWindow) firstTranslatedOverlayMs=$firstOverlay status=[$($final.status)] (trace: T-10_trace.csv)"
    } catch { Record 'T-10' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }

    if ($WithKeyCases -and (Want 'A-04')) { try {
        # R-01 + user's fix: full-screen region leaves no room for the caption or the result window.
        # Expected (fixed build): first line translated, then auto stops with the "no free space" warning instead of OCR-ing its own result window.
        Use-QaBaseSettings (Rect 0 0 2560 1440) $true
        $lines = @('The door is locked.', 'Your health is low.', 'Thank you for your help.')
        $ev = Start-Game @{ x = 0; y = 0; w = 2560; h = 1440; lines = $lines; holdMs = 7000; fontSize = 40 } 'a04'
        [void](Start-App)
        Hotkey 'P'
        $watch = Watch-App 24000 60
        $status = Get-TransStatus
        $res = Get-Hwnd '자막 번역 결과' $true; $resRect = if ($res -ne [IntPtr]::Zero) { $r = Get-WinRect $res; "($($r.Left),$($r.Top))-($($r.Right),$($r.Bottom))" }
        $autoAfter = Get-AutoOn
        if ($autoAfter) { Hotkey 'P' }
        $read = @($lines | Where-Object { $l = $_; [bool]($watch | Where-Object { (Sim $_.text $l) -ge 0.8 }) })
        foreach ($l in $read) { Add-DeeplCall 'A-04' $l.Length 'app auto (read in OCR text; sent if consent+key ok)' }
        $selfRead = @($watch | Where-Object { $_.text -match '번역|결과|자막' }).Count
        Save-Rows $watch 'A-04_watch.csv'
        $stopped = ($autoAfter -eq $false) -and ($status -match '빈 공간이 없어')
        Record 'A-04' $(if ($stopped -and $selfRead -eq 0) { 'PASS' } else { 'FAIL' }) "full-screen region: lines read=$($read.Count)/3 autoAfter=$autoAfter stoppedWithWarning=$stopped resultWindow=$resRect ownTextRead=$selfRead status=[$status]"
    } catch { Record 'A-04' 'ERROR' $_.Exception.Message } finally { Stop-App; Stop-Game } }
}
finally {
    Stop-App; Stop-Game
    if (!$NoRestore) { Restore-UserSettings }
    Save-Rows $summary 'summary.csv'
    Log "suite-app done: $(@($summary | Where-Object result -eq 'PASS').Count) pass, $(@($summary | Where-Object result -eq 'FAIL').Count) fail, $(@($summary | Where-Object result -eq 'ERROR').Count) error"
}
