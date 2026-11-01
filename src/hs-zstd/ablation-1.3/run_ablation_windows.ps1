# hs_zstd 1.3 ablation on the desktop: 12 jobs, 5 decode rounds, SHA-256 verified, peak working set recorded.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$jobs = 12; $rounds = 5
$dc = Join-Path $HOME "Documents\data compression\HCM-tests"
$old = Join-Path $dc "pzstd-2026-10-31"
Copy-Item (Join-Path $old "zstd.exe") . -Force; Copy-Item (Join-Path $old "pzstd.exe") . -Force; Copy-Item (Join-Path $old "hs_zstd.exe") .\hs_zstd12.exe -Force
if (!(Test-Path dup_random_chunks.bin)) { cmd /c "copy /b dup.part0+dup.part1 dup_random_chunks.bin > nul"; Remove-Item dup.part0, dup.part1 }
if (!(Test-Path silesia_joined.bin)) {
  $s = Join-Path $dc "v36-cores-silesia-2026-10-30"; $m = Join-Path $dc "v36-cores-2026-10-30"
  $parts = @("$s\dickens","$m\mozilla","$s\mr","$s\nci","$s\ooffice","$s\osdb","$s\reymont","$m\samba","$s\sao","$s\webster","$s\x-ray","$s\xml")
  $out = [System.IO.File]::Create("$PWD\silesia_joined.bin"); foreach ($p in $parts) { $b = [System.IO.File]::ReadAllBytes($p); $out.Write($b, 0, $b.Length) }; $out.Close()
}
function Run($exe, $a) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $exe -ArgumentList $a -NoNewWindow -PassThru -RedirectStandardOutput "nul_out.txt" -RedirectStandardError "nul_err.txt"
  $peak = 0
  while (-not $p.HasExited) { try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}; Start-Sleep -Milliseconds 5 }
  $sw.Stop(); try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
  $p.WaitForExit(); $code = $p.ExitCode
  if ($null -ne $code -and $code -ne 0) { throw "$exe $a failed (exit $code): $(Get-Content nul_err.txt -Raw)" }
  return @($sw.Elapsed.TotalSeconds, [math]::Round($peak / 1MB, 1))
}
$files = @("switch_1mb.bin", "dup_random_chunks.bin", "silesia_joined.bin")
$variants = @(
  @("1.3 default (Sets, raw off, safe), 16 MB",   "--block-mb 16"),
  @("plain blocks --sets 0, 16 MB",               "--block-mb 16 --sets 0"),
  @("1.2 rule (--raw entropy --smooth 1.2), 16 MB","--block-mb 16 --raw entropy --smooth 1.2"),
  @("Sets --raw test, 16 MB",                     "--block-mb 16 --raw test"),
  @("1.4 --merge 0.1 (close centres merged), 16 MB","--block-mb 16 --merge 0.1"),
  @("1.4 --autok 0.2 (sets by separation), 16 MB", "--block-mb 16 --autok 0.2"),
  @("1.4 --autok 0.3 (sets by separation), 16 MB", "--block-mb 16 --autok 0.3"),
  @("1.3 default (Sets, raw off, safe), 4 MB",    "--block-mb 4"),
  @("plain blocks --sets 0, 4 MB",                "--block-mb 4 --sets 0"),
  @("1.2 rule (--raw entropy --smooth 1.2), 4 MB", "--block-mb 4 --raw entropy --smooth 1.2")
)
"hs_zstd 1.3/1.4-test ablation, desktop, $(Get-Date), jobs=$jobs, decode rounds=$rounds, --flush on, SHA-256 verified"
"CPU: $((Get-CimInstance Win32_Processor).Name), threads $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors), RAM $([math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB)) GB"
$rows = @()
foreach ($f in $files) {
  $n = (Get-Item $f).Length; $ref = (Get-FileHash $f -Algorithm SHA256).Hash
  ""; "=== $f  ($('{0:N0}' -f $n) bytes)"
  $jobsList = @()
  foreach ($v in $variants) { $jobsList += ,@($v[0], ".\hs_zstd14.exe", "c `"$f`" c.bin --jobs $jobs $($v[1])", ".\hs_zstd14.exe", "d c.bin o.bin --jobs $jobs --flush") }
  $jobsList += ,@("zstd -3, one thread", ".\zstd.exe", "-3 -q -f `"$f`" -o c.bin", ".\zstd.exe", "-d -q -f c.bin -o o.bin")
  $jobsList += ,@("zstd -3 -T$jobs", ".\zstd.exe", "-3 -T$jobs -q -f `"$f`" -o c.bin", ".\zstd.exe", "-d -q -f c.bin -o o.bin")
  $jobsList += ,@("zstd -3 --long=27, one thread", ".\zstd.exe", "-3 --long=27 -q -f `"$f`" -o c.bin", ".\zstd.exe", "-d -q -f --long=27 c.bin -o o.bin")
  $jobsList += ,@("pzstd -3 -p $jobs", ".\pzstd.exe", "-3 -q -f -p $jobs `"$f`" -o c.bin", ".\pzstd.exe", "-d -q -f -p $jobs c.bin -o o.bin")
  foreach ($j in $jobsList) {
    $name = $j[0]
    if (Test-Path c.bin) { Remove-Item c.bin }
    $c = Run $j[1] $j[2]; $size = (Get-Item c.bin).Length
    $dts = @(); $dpeak = 0
    for ($r = 0; $r -lt $rounds; $r++) {
      if (Test-Path o.bin) { Remove-Item o.bin }
      $d = Run $j[3] $j[4]; $dts += $d[0]; if ($d[1] -gt $dpeak) { $dpeak = $d[1] }
      if ((Get-FileHash o.bin -Algorithm SHA256).Hash -ne $ref) { throw "ROUND TRIP FAILED: $f $name" }
    }
    $med = ($dts | Sort-Object)[[int][math]::Floor($rounds / 2)]
    $line = "{0,-48} bits/M {1,9:N0}  bytes {2,13:N0}  comp {3,7:N2} s ({4,6} MB)  decode med {5,6:N3} s  all [{6}]  peak {7,6} MB" -f $name, [math]::Round(1e6 * $size / $n), $size, $c[0], $c[1], $med, (($dts | ForEach-Object { '{0:N3}' -f $_ }) -join ' '), $dpeak
    $line; $rows += [pscustomobject]@{file=$f; variant=$name; bytes=$size; per_million=[math]::Round(1e6*$size/$n); compress_s=[math]::Round($c[0],3); compress_peak_mb=$c[1]; decode_median_s=[math]::Round($med,3); decode_all_s=(($dts | ForEach-Object { '{0:N3}' -f $_ }) -join ' '); decode_peak_mb=$dpeak}
  }
  # format compatibility: file written by 1.3 defaults must decode with the published 1.2 exe
  if (Test-Path c.bin) { Remove-Item c.bin }; $null = Run ".\hs_zstd14.exe" "c `"$f`" c.bin --jobs $jobs --block-mb 16"
  if (Test-Path o.bin) { Remove-Item o.bin }; $null = Run ".\hs_zstd12.exe" "d c.bin o.bin --jobs $jobs"
  if ((Get-FileHash o.bin -Algorithm SHA256).Hash -eq $ref) { "1.3-made file decoded exactly by the 1.2 exe: yes" } else { "1.3-made file decoded by 1.2 exe: MISMATCH" }
}
$rows | Export-Csv ablation_results_desktop.csv -NoTypeInformation
Remove-Item c.bin, o.bin, nul_out.txt, nul_err.txt -ErrorAction SilentlyContinue
""; "FINISHED $(Get-Date)"
