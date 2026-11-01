# Core-scaling curve: hs_zstd 1.3 (4 MB and 16 MB blocks) and pzstd at 1, 2, 4, 6, 8, 12 jobs on the 4 GB public Silesia file.
# Compress once per N, decode 3 rounds per N (median), SHA-256 checked on the first decode of each N.
$ErrorActionPreference = "Continue"; Set-Location $PSScriptRoot
$src = Join-Path $HOME "Documents\data compression\HCM-tests\race4gb-2026-11-01"
$f = Join-Path $src "silesia_4gb.bin"; $pz = Join-Path $src "pzstd.exe"
if (!(Test-Path $f)) { "missing $f"; exit 1 }
$n = (Get-Item $f).Length; $ref = (Get-FileHash $f -Algorithm SHA256).Hash
function Run($exe, $a) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $exe -ArgumentList $a -NoNewWindow -PassThru -RedirectStandardOutput "o_out.txt" -RedirectStandardError "o_err.txt"
  $p.WaitForExit(); $sw.Stop(); $c = $p.ExitCode
  if ($null -ne $c -and $c -ne 0) { "FAILED ($c): $exe $a  $(Get-Content o_err.txt -Raw)" }
  return $sw.Elapsed.TotalSeconds
}
"core scaling, desktop, $(Get-Date), file silesia_4gb.bin $n bytes, sha256 $ref"
"CPU: $((Get-CimInstance Win32_Processor).Name), threads $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)"
"tool                 jobs  compress s  decode s (3 rounds)        median   1,000,000 bits down to  exact"
foreach ($cfg in @(@("hs_zstd 1.3, 4 MB", "4"), @("hs_zstd 1.3, 16 MB", "16"), @("pzstd -3", "pz"))) {
  foreach ($j in @(1, 2, 4, 6, 8, 12)) {
    Remove-Item c.bin, o.bin -ErrorAction SilentlyContinue
    if ($cfg[1] -eq "pz") { $ct = Run $pz "-3 -q -f -p $j `"$f`" -o c.bin" } else { $ct = Run ".\hs_zstd13.exe" "c `"$f`" c.bin --jobs $j --block-mb $($cfg[1])" }
    $size = (Get-Item c.bin).Length; $ds = @(); $ok = "?"
    for ($r = 0; $r -lt 3; $r++) {
      Remove-Item o.bin -ErrorAction SilentlyContinue
      if ($cfg[1] -eq "pz") { $ds += Run $pz "-d -q -f -p $j c.bin -o o.bin" } else { $ds += Run ".\hs_zstd13.exe" "d c.bin o.bin --jobs $j --flush" }
      if ($r -eq 0) { $ok = ((Get-FileHash o.bin -Algorithm SHA256).Hash -eq $ref) }
    }
    $med = ($ds | Sort-Object)[1]
    "{0,-20} {1,4}  {2,10:N2}  {3,-25}  {4,6:N2}   {5,22:N0}  {6}" -f $cfg[0], $j, $ct, (($ds | ForEach-Object { '{0:N2}' -f $_ }) -join ' / '), $med, [math]::Round(1e6 * $size / $n), $ok
  }
}
Remove-Item c.bin, o.bin, o_out.txt, o_err.txt -ErrorAction SilentlyContinue
"FINISHED $(Get-Date)"
