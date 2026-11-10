# hx-1.1.0-pc-test.ps1 - HX 1.1.0 test on Michael's PC (10 Nov 2026). Michael Hugh Harrington. Same license as HX.
# Reads public test data in place (Calgary, Squash Snappy, enwik8); everything it makes stays in this folder's "work"
# folder, which is deleted at the end. Results: results-HX-1.1.0-PC.txt beside this script.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dc   = Split-Path -Parent (Split-Path -Parent $here)          # ...\data compression
$td   = Join-Path $dc 'HCM-tests\hx-competition-2026-11-08\testdata'
$new  = Join-Path $here 'hx-1.1.0.exe'
$old  = Join-Path $here 'hx-1.0.1.exe'
$work = Join-Path $here 'work'
$res  = Join-Path $here 'results-HX-1.1.0-PC.txt'
$T    = [Environment]::ProcessorCount
New-Item -ItemType Directory -Force $work | Out-Null
function Say($s) { Write-Host $s; Add-Content -Path $res -Value $s }
function Sha($f) { (Get-FileHash -Algorithm SHA256 $f).Hash.ToLower() }
function Same($a, $b) { (Get-Item $a).Length -eq (Get-Item $b).Length -and (Sha $a) -eq (Sha $b) }
# runs one program, returns seconds and peak working set (MiB, sampled every 50 ms)
function Run($exe, $argline) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe, $argline
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $sw = [Diagnostics.Stopwatch]::StartNew(); $p = [Diagnostics.Process]::Start($psi)
    $so = $p.StandardOutput.ReadToEndAsync(); $se = $p.StandardError.ReadToEndAsync(); $peak = 0
    while (-not $p.HasExited) { try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}; Start-Sleep -Milliseconds 50 }
    $p.WaitForExit(); $sw.Stop()
    [pscustomobject]@{ s = $sw.Elapsed.TotalSeconds; mib = $peak / 1MB; rc = $p.ExitCode; out = ($so.Result + $se.Result).Trim() }
}
# runs one cmd.exe line from a small .cmd file (so quotes, < > and | reach cmd.exe as written)
function Cmd($line) { $f = Join-Path $work 'step.cmd'; Set-Content -Path $f -Value "@echo off`r`n$line" -Encoding ASCII
    $sw = [Diagnostics.Stopwatch]::StartNew(); & cmd.exe /c $f; $rc = $LASTEXITCODE; $sw.Stop(); [pscustomobject]@{ s = $sw.Elapsed.TotalSeconds; rc = $rc } }
function Q($p) { '"' + $p + '"' }

Remove-Item $res -ErrorAction SilentlyContinue
Say "HX 1.1.0 - TEST ON MICHAEL'S PC   $(Get-Date -Format 'yyyy-MM-dd HH:mm')   $T logical cores"
Say ""
# 1. verify the programs and data are the intended versions
Say "1. VERIFY"
$hn = Sha $new; $ho = Sha $old
Say ("   hx-1.1.0.exe  " + $hn + $(if ($hn -eq '2a257be2b840980bd8db4596546509f90efc4a0b12d6fa3fd1c8ad962ee9a205') { '  OK' } else { '  WRONG FILE' }))
Say ("   hx-1.0.1.exe  " + $ho + $(if ($ho.StartsWith('0961469b22f87135')) { '  OK (Competition Test app copy)' } else { '  WRONG FILE' }))
$e8 = Join-Path $td 'enwik8\enwik8'
$m8 = (Get-FileHash -Algorithm MD5 $e8).Hash.ToLower()
Say ("   enwik8 MD5    " + $m8 + $(if ($m8 -eq 'a1fa5ffddb56f4953e226637dabbb36a') { '  OK' } else { '  WRONG FILE' }))
if (-not $hn.StartsWith('2a257be2') -or -not $ho.StartsWith('0961469b') -or $m8 -ne 'a1fa5ffddb56f4953e226637dabbb36a') { Say "   STOPPED: a file is not the intended version"; exit 1 }
Say ""

# 2. HX 1.0 format unchanged, and HX 1.0 files decode
Say "2. SAME OUTPUT AS HX 1.0.1 (no new flags) AND HX 1.0.1 FILES DECODE WITH 1.1.0  (Calgary 18 + Snappy 7 files)"
$same = 0; $dec = 0; $n = 0
foreach ($f in (Get-ChildItem (Join-Path $td 'calgary'), (Join-Path $td 'squash-snappy') -File)) {
    $n++; $a = Join-Path $work 'a.hx'; $b = Join-Path $work 'b.hx'; $o = Join-Path $work 'o.out'
    Run $old "c $(Q $f.FullName) $(Q $a)" | Out-Null; Run $new "c $(Q $f.FullName) $(Q $b)" | Out-Null
    if (Same $a $b) { $same++ } else { Say "   DIFFERENT: $($f.Name)" }
    Run $new "d $(Q $a) $(Q $o)" | Out-Null; if (Same $o $f.FullName) { $dec++ } else { Say "   DECODE FAILED: $($f.Name)" }
}
Say "   identical output: $same of $n     HX 1.0.1 files decoded exactly by 1.1.0: $dec of $n"
Say ""

# 3. enwik8 with HX Fast: whole file vs block mode vs memory limit
Say "3. ENWIK8 (100,000,000 bytes), HX FAST, $T threads"
Say "   run                          bytes        ratio    compress s  peak MiB   decode s  peak MiB  exact"
$runs = @(@('whole file (HX 1.0 format)', ''), @('--block=64M', '--block=64M'), @('--memlimit=1G', '--memlimit=1G'), @('--memlimit=4G', '--memlimit=4G'))
foreach ($r in $runs) {
    $c = Join-Path $work 'e.hx'; $o = Join-Path $work 'e.out'
    $rc = Run $new "c $(Q $e8) $(Q $c) $($r[1])"; $rd = Run $new "d $(Q $c) $(Q $o)"
    $sz = (Get-Item $c).Length; $ok = if (Same $o $e8) { 'yes' } else { 'NO' }
    if ($r[1] -eq '--block=64M') { Copy-Item $c (Join-Path $work 'b64.hx') }
    $blk = ($rc.out -split "`n" | Where-Object { $_ -like 'hx: block mode*' }) -join ''
    Say ("   {0,-28} {1,11:N0}  {2,7:N3}x  {3,9:N1}  {4,8:N0}  {5,9:N2}  {6,8:N0}  {7}" -f $r[0], $sz, (1e8 / $sz), $rc.s, $rc.mib, $rd.s, $rd.mib, $ok)
    if ($blk) { Say "      ($blk)" }
    Remove-Item $o
}
Say ""

# 4. pipes (cmd.exe redirection and a real pipe between two hx processes)
Say "4. PIPES (enwik8, 64M blocks)"
$b64 = Join-Path $work 'b64.hx'; $p1 = Join-Path $work 'p1.hx'; $p2 = Join-Path $work 'p2.hx'; $po = Join-Path $work 'p.out'
$x = Cmd "$(Q $new) c - $(Q $p1) --block=64M < $(Q $e8) 2>nul"
Say ("   stdin -> file:            same bytes as file mode: " + $(if (Same $p1 $b64) { 'yes' } else { 'NO' }) + ("   {0:N1} s" -f $x.s))
$x = Cmd "$(Q $new) d $(Q $p1) - > $(Q $po) 2>nul"
Say ("   file -> stdout decode:    exact: " + $(if (Same $po $e8) { 'yes' } else { 'NO' }) + ("   {0:N2} s" -f $x.s)); Remove-Item $po
$x = Cmd "$(Q $new) d $(Q $p1) - 2>nul | $(Q $new) c - $(Q $p2) --block=64M 2>nul"
Say ("   hx d | hx c (real pipe):  same bytes again: " + $(if (Same $p2 $b64) { 'yes' } else { 'NO' }) + ("   {0:N1} s" -f $x.s))
Say ""

# 5. HX Complete with a memory limit (skipped after 14:20 so it cannot overlap the 3 PM corpus run)
Say "5. ENWIK8, HX COMPLETE, --memlimit=2G, $T threads"
if ((Get-Date).TimeOfDay -lt [TimeSpan]'14:20:00') {
    $c = Join-Path $work 'c.hx'; $o = Join-Path $work 'c.out'
    $rc = Run $new "c $(Q $e8) $(Q $c) --complete --memlimit=2G"; $rd = Run $new "d $(Q $c) $(Q $o)"
    $sz = (Get-Item $c).Length; $ok = if (Same $o $e8) { 'yes' } else { 'NO' }
    $blk = ($rc.out -split "`n" | Where-Object { $_ -like 'hx: block mode*' }) -join ''
    Say ("   {0,11:N0} bytes  {1:N3}x  compress {2:N1} s  peak {3:N0} MiB  decode {4:N2} s  peak {5:N0} MiB  exact {6}" -f $sz, (1e8 / $sz), $rc.s, $rc.mib, $rd.s, $rd.mib, $ok)
    if ($blk) { Say "   ($blk)" }
} else { Say "   skipped (started after 14:20)" }
Say ""
Say "Finished $(Get-Date -Format 'yyyy-MM-dd HH:mm'). Peak MiB = Windows peak working set, sampled every 50 ms."
Remove-Item -Recurse -Force $work
