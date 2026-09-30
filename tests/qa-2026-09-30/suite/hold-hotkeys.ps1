# D-01 helper: holds Ctrl+Alt+T/P/H/F8/F9 (and M if free) so the app's RegisterHotKey fails. Exits after 60 s.
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class HoldKeys { [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr h, int id, uint mods, uint vk); }
'@
$mods = 0x0001 -bor 0x0002 -bor 0x4000   # ALT | CONTROL | NOREPEAT
$id = 100
foreach ($vk in 0x54, 0x50, 0x4D, 0x77, 0x48, 0x78) { [void][HoldKeys]::RegisterHotKey([IntPtr]::Zero, $id++, $mods, $vk) }
Start-Sleep -Seconds 60
