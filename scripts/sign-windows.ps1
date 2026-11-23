# Signs every unsigned .exe and .dll of the Windows build with your code-signing certificate (P-22).
# Files that already carry a valid signature (Qt, ONNX Runtime) are left as they are. Smart App Control
# can block the whole app for one unsigned file, so the script fails if any file is still unsigned.
#
#   powershell -ExecutionPolicy Bypass -File scripts\sign-windows.ps1 -Thumbprint <SHA-1 of the certificate>
#
# The certificate must be in Cert:\CurrentUser\My or Cert:\LocalMachine\My (a hardware token or a cloud
# signing service installs it there). A self-signed certificate only tests this script: Smart App Control
# and SmartScreen do not trust it.
param(
    [Parameter(Mandatory = $true)][string]$Thumbprint,
    [string]$Dist = (Join-Path (Split-Path $PSScriptRoot) 'dist\windows'),
    [string]$TimestampUrl = 'http://timestamp.digicert.com', # RFC 3161; use your CA's server if it has one
    [switch]$NoTimestamp,  # tests only: a signature without a timestamp stops being valid when the certificate expires
    [string]$SignTool = ''
)
$ErrorActionPreference = 'Stop'
if (-not $SignTool) {
    $SignTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $SignTool -or -not (Test-Path $SignTool)) { throw 'signtool.exe not found. Install the Windows SDK or pass -SignTool.' }
$Thumbprint = ($Thumbprint -replace '\s', '').ToUpperInvariant()
$cert = @(Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint })[0]
if (-not $cert) { throw "No certificate with thumbprint $Thumbprint in CurrentUser\My or LocalMachine\My." }
if (-not $cert.HasPrivateKey) { throw 'The certificate has no private key on this PC.' }
if (-not ($cert.EnhancedKeyUsageList | Where-Object { $_.ObjectId -eq '1.3.6.1.5.5.7.3.3' })) { throw 'The certificate is not for code signing.' }
if (-not (Test-Path $Dist)) { throw "No build at $Dist. Run scripts\build-windows.ps1 first." }

$files = Get-ChildItem $Dist -Recurse -Include *.exe, *.dll -File
$toSign = @(); $kept = 0
foreach ($file in $files) {
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -eq 'Valid' -and $signature.SignerCertificate.Thumbprint -ne $Thumbprint) { $kept++ } else { $toSign += $file.FullName }
}
"files: $($files.Count), already signed by their makers: $kept, to sign: $($toSign.Count)"
$arguments = @('sign', '/fd', 'SHA256', '/sha1', $Thumbprint)
if ($cert.PSParentPath -match 'LocalMachine') { $arguments += '/sm' }
if (-not $NoTimestamp) { $arguments += @('/tr', $TimestampUrl, '/td', 'SHA256') }
for ($i = 0; $i -lt $toSign.Count; $i += 20) { # signtool takes many files at once; keep the command line short
    $batch = $toSign[$i..([Math]::Min($i + 19, $toSign.Count - 1))]
    & $SignTool @arguments @batch | Out-Null
    if ($LASTEXITCODE) { throw "signtool failed (exit $LASTEXITCODE) on files $i..$($i + $batch.Count - 1)." }
}
# What Windows sees now. A self-signed test certificate shows as UnknownError (untrusted root).
$unsigned = @(); $ours = 0
foreach ($file in $files) {
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.SignerCertificate -and $signature.SignerCertificate.Thumbprint -eq $Thumbprint) { $ours++ }
    elseif ($signature.Status -ne 'Valid') { $unsigned += $file.FullName.Substring($Dist.Length + 1) }
}
"signed with this certificate: $ours, still unsigned: $($unsigned.Count)"
if ($unsigned.Count) { $unsigned | ForEach-Object { "  $_" }; exit 1 }
