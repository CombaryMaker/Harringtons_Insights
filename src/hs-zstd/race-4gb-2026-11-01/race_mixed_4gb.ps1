# 4 GB race: hs_zstd 1.3 vs plain block splitting vs zstd / zstd -T12 / zstd --long / pzstd. 12 jobs, 5 decode rounds,
# --flush for hs_zstd, SHA-256 verified round trips, peak working set recorded.
$ErrorActionPreference = "Stop"; Set-Location $PSScriptRoot
$jobs = 12; $rounds = 5
$old = Join-Path $HOME "Documents\data compression\HCM-tests\pzstd-2026-10-31"
Copy-Item (Join-Path $old "zstd.exe") . -Force; Copy-Item (Join-Path $old "pzstd.exe") . -Force
function Run($exe, $a) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $exe -ArgumentList $a -NoNewWindow -PassThru -RedirectStandardOutput "nul_out.txt" -RedirectStandardError "nul_err.txt"
  $peak = 0; $pc = 0
  while (-not $p.HasExited) { try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 }; if ($p.PeakPagedMemorySize64 -gt $pc) { $pc = $p.PeakPagedMemorySize64 } } catch {}; Start-Sleep -Milliseconds 20 }
  $sw.Stop(); try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 }; if ($p.PeakPagedMemorySize64 -gt $pc) { $pc = $p.PeakPagedMemorySize64 } } catch {}
  $p.WaitForExit(); $code = $p.ExitCode
  if ($null -ne $code -and $code -ne 0) { throw "$exe $a failed (exit $code): $(Get-Content nul_err.txt -Raw)" }
  return @($sw.Elapsed.TotalSeconds, [math]::Round($peak / 1MB, 1), [math]::Round($pc / 1MB, 1))
}
$files = @("mixed_4gb.tar")
$variants = @(
  @("hs_zstd 1.3 (Sets, raw off, safe), 16 MB", "--block-mb 16"),
  @("plain blocks --sets 0, 16 MB",             "--block-mb 16 --sets 0"),
  @("1.2 rule (--raw entropy --smooth 1.2), 16 MB", "--block-mb 16 --raw entropy --smooth 1.2"),
  @("hs_zstd 1.3 (Sets, raw off, safe), 4 MB",  "--block-mb 4"),
  @("plain blocks --sets 0, 4 MB",              "--block-mb 4 --sets 0"))
"4 GB race, desktop, $(Get-Date), jobs=$jobs, decode rounds=$rounds, hs_zstd with --flush, SHA-256 verified"
"CPU: $((Get-CimInstance Win32_Processor).Name), threads $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors), RAM $([math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB)) GB"
$rows = @()
foreach ($f in $files) {
  $n = (Get-Item $f).Length; $ref = (Get-FileHash $f -Algorithm SHA256).Hash
  ""; "=== $f  ($('{0:N0}' -f $n) bytes)  sha256 $ref"
  $jl = @()
  foreach ($v in $variants) { $jl += ,@($v[0], ".\hs_zstd14.exe", "c `"$f`" c.bin --jobs $jobs $($v[1])", ".\hs_zstd14.exe", "d c.bin o.bin --jobs $jobs --flush") }
  $jl += ,@("zstd -3, one thread", ".\zstd.exe", "-3 -q -f `"$f`" -o c.bin", ".\zstd.exe", "-d -q -f c.bin -o o.bin")
  $jl += ,@("zstd -3 -T$jobs", ".\zstd.exe", "-3 -T$jobs -q -f `"$f`" -o c.bin", ".\zstd.exe", "-d -q -f c.bin -o o.bin")
  $jl += ,@("zstd -3 --long=27 -T$jobs", ".\zstd.exe", "-3 --long=27 -T$jobs -q -f `"$f`" -o c.bin", ".\zstd.exe", "-d -q -f --long=27 c.bin -o o.bin")
  $jl += ,@("pzstd -3 -p $jobs", ".\pzstd.exe", "-3 -q -f -p $jobs `"$f`" -o c.bin", ".\pzstd.exe", "-d -q -f -p $jobs c.bin -o o.bin")
  foreach ($j in $jl) {
    $name = $j[0]; if (Test-Path c.bin) { Remove-Item c.bin }
    $c = Run $j[1] $j[2]; $size = (Get-Item c.bin).Length; $dts = @(); $dpeak = 0; $dcommit = 0
    for ($r = 0; $r -lt $rounds; $r++) {
      if (Test-Path o.bin) { Remove-Item o.bin }
      $d = Run $j[3] $j[4]; $dts += $d[0]; if ($d[1] -gt $dpeak) { $dpeak = $d[1] }; if ($d[2] -gt $dcommit) { $dcommit = $d[2] }
      if ((Get-FileHash o.bin -Algorithm SHA256).Hash -ne $ref) { throw "ROUND TRIP FAILED: $f $name" }
    }
    $med = ($dts | Sort-Object)[[int][math]::Floor($rounds / 2)]
    "{0,-46} bits/M {1,9:N0}  bytes {2,15:N0}  comp {3,7:N2} s ({4,7} MB)  decode med {5,6:N2} s  all [{6}]  working set {7,7} MB  private commit {8,7} MB" -f $name, [math]::Round(1e6 * $size / $n), $size, $c[0], $c[1], $med, (($dts | ForEach-Object { '{0:N2}' -f $_ }) -join ' '), $dpeak, $dcommit
    $rows += [pscustomobject]@{file=$f; variant=$name; bytes=$size; per_million=[math]::Round(1e6*$size/$n); compress_s=[math]::Round($c[0],2); compress_peak_mb=$c[1]; decode_median_s=[math]::Round($med,2); decode_all_s=(($dts | ForEach-Object { '{0:N2}' -f $_ }) -join ' '); decode_peak_workingset_mb=$dpeak; decode_peak_commit_mb=$dcommit}
  }
}
$rows | Export-Csv race_4gb_mixed_results.csv -NoTypeInformation
Remove-Item c.bin, o.bin, nul_out.txt, nul_err.txt -ErrorAction SilentlyContinue
""; "FINISHED $(Get-Date)"
