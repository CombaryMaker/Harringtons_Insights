# HCM v3.6.1 decoder memory and time on Silesia, using the .hcm files already made by v3.6 on this desktop
# (decoder unchanged in 3.6.1): single stream (.c), parallel blocks 16 MB (.p), Harrington Sets 16 MB (.s), Sets 4 MB (.s4).
# Peak private memory (PeakPagedMemorySize) and peak working set sampled every 10 ms; every output SHA-256 checked.
$ErrorActionPreference = "Continue"; Set-Location $PSScriptRoot
$a = Join-Path $HOME "Documents\data compression\HCM-tests\v36-cores-silesia-2026-10-30"
$b = Join-Path $HOME "Documents\data compression\HCM-tests\v36-cores-2026-10-30"
$files = @(@($a,"dickens"),@($b,"mozilla"),@($a,"mr"),@($a,"nci"),@($a,"ooffice"),@($a,"osdb"),@($a,"reymont"),@($b,"samba"),@($a,"sao"),@($a,"webster"),@($a,"x-ray"),@($a,"xml"))
function Dec($inp) {
  Remove-Item o.bin -ErrorAction SilentlyContinue
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath ".\HCM361.exe" -ArgumentList "d `"$inp`" o.bin" -NoNewWindow -PassThru -RedirectStandardOutput "d_out.txt" -RedirectStandardError "d_err.txt"
  $ws = 0; $pc = 0
  while (-not $p.HasExited) { try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $ws) { $ws = $p.PeakWorkingSet64 }; if ($p.PeakPagedMemorySize64 -gt $pc) { $pc = $p.PeakPagedMemorySize64 } } catch {}; Start-Sleep -Milliseconds 10 }
  $sw.Stop(); return @($sw.Elapsed.TotalSeconds, [math]::Round($ws / 1MB, 1), [math]::Round($pc / 1MB, 1))
}
"HCM v3.6.1 decoder memory, desktop, $(Get-Date)"
"CPU: $((Get-CimInstance Win32_Processor).Name), threads $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)"
"file       variant          bytes in   decode s   peak private MB   peak working set MB   exact"
$tot = @{}
foreach ($x in $files) {
  $orig = Join-Path $x[0] $x[1]; $ref = (Get-FileHash $orig -Algorithm SHA256).Hash
  foreach ($v in @(@("c","single stream"),@("p","blocks 16 MB"),@("s","Sets 16 MB"),@("s4","Sets 4 MB"))) {
    $h = Join-Path $x[0] ($x[1] + "." + $v[0] + ".hcm"); if (!(Test-Path $h)) { "{0,-10} {1,-16} missing" -f $x[1], $v[1]; continue }
    $r = Dec $h; $ok = ((Get-FileHash o.bin -Algorithm SHA256).Hash -eq $ref)
    "{0,-10} {1,-16} {2,10:N0}   {3,7:N1}   {4,15:N1}   {5,19:N1}   {6}" -f $x[1], $v[1], (Get-Item $h).Length, $r[0], $r[2], $r[1], $ok
    if (!$tot[$v[1]]) { $tot[$v[1]] = @(0.0, 0.0) }; $tot[$v[1]][0] += $r[0]; if ($r[2] -gt $tot[$v[1]][1]) { $tot[$v[1]][1] = $r[2] }
  }
}
""; "TOTALS (sum of decode s, highest peak private MB):"
foreach ($k in @("single stream","blocks 16 MB","Sets 16 MB","Sets 4 MB")) { if ($tot[$k]) { "  {0,-16} {1,7:N1} s   {2,7:N1} MB" -f $k, $tot[$k][0], $tot[$k][1] } }
Remove-Item o.bin, d_out.txt, d_err.txt -ErrorAction SilentlyContinue
"FINISHED $(Get-Date)"
