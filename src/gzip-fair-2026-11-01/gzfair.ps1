# Fair gzip baselines (Andrew Goodwin's list): gzip, pigz, bgzip -@ 12, rapidgzip -P 12 (on plain gzip AND on bgzip
# output), and hs_wrap 1.5 + gzip -6 (Sets and the --sets 0 plain-blocks control). 4 GB public Silesia file, 12 threads,
# 3 decode rounds each, every decode SHA-256 checked. rapidgzip runs from a portable Python kept inside this folder.
$ErrorActionPreference = "Continue"; Set-Location $PSScriptRoot
$jobs = 12; $rounds = 3
$f = Join-Path $HOME "Documents\data compression\HCM-tests\race4gb-2026-11-01\silesia_4gb.bin"
if (!(Test-Path $f)) { "missing $f"; exit 1 }
$old = Join-Path $HOME "Documents\data compression\HCM-tests\pigz-2026-10-31"
Copy-Item (Join-Path $old "gzip.exe") . -Force; Copy-Item (Join-Path $old "pigz.exe") . -Force
# portable Python + rapidgzip (official python.org embeddable zip and PyPI; nothing installed system-wide)
if (!(Test-Path py\Scripts\rapidgzip.exe)) {
  & curl.exe -L -sS -o py.zip https://www.python.org/ftp/python/3.12.7/python-3.12.7-embed-amd64.zip
  Expand-Archive py.zip -DestinationPath py -Force; Remove-Item py.zip
  (Get-Content py\python312._pth) -replace '^#import site', 'import site' | Set-Content py\python312._pth
  & curl.exe -L -sS -o py\get-pip.py https://bootstrap.pypa.io/get-pip.py
  & py\python.exe py\get-pip.py --no-warn-script-location -q 2>&1 | Out-Null
  & py\python.exe -m pip install rapidgzip --no-warn-script-location -q 2>&1 | Out-Null
}
$rg = Join-Path $PWD "py\Scripts\rapidgzip.exe"
if (!(Test-Path $rg)) { "rapidgzip install failed"; }
"versions: $(& .\gzip.exe --version 2>&1 | Select-Object -First 1) | $(& .\pigz.exe --version 2>&1) | $(& .\bgzip.exe --version 2>&1 | Select-Object -First 1) | rapidgzip $(& $rg --version 2>&1 | Select-Object -First 1)"
$n = (Get-Item $f).Length; $ref = (Get-FileHash $f -Algorithm SHA256).Hash
"fair gzip baselines, desktop, $(Get-Date), silesia_4gb.bin $n bytes, sha256 $ref, $jobs threads, $rounds decode rounds"
"CPU: $((Get-CimInstance Win32_Processor).Name), threads $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)"
function C($cmd) { $sw = [Diagnostics.Stopwatch]::StartNew(); cmd /c "$cmd 2> err.txt"; $sw.Stop(); if ($LASTEXITCODE) { "  FAILED: $cmd  $(Get-Content err.txt -Raw)" | Write-Host }; return $sw.Elapsed.TotalSeconds }
function Line($name, $ct, $file, $dcmd) {
  $size = (Get-Item $file).Length; $ds = @(); $ok = $true
  for ($r = 0; $r -lt $rounds; $r++) { Remove-Item o.bin -ErrorAction SilentlyContinue; $ds += C $dcmd; if ((Get-FileHash o.bin -Algorithm SHA256).Hash -ne $ref) { $ok = $false } }
  $med = ($ds | Sort-Object)[[int][math]::Floor($rounds / 2)]
  "{0,-44} {1,9:N0}  comp {2,7:N1} s  decode {3,-20}  median {4,6:N2} s  exact {5}" -f $name, [math]::Round(1e6 * $size / $n), $ct, (($ds | ForEach-Object { '{0:N2}' -f $_ }) -join ' / '), $med, $ok
}
"tool                                         1,000,000 bits down to"
$ct = C ".\gzip.exe -6 -c `"$f`" > g.gz";                 Line "gzip -6, one thread (decode gzip)" $ct g.gz ".\gzip.exe -d -c g.gz > o.bin"
                                                           Line "  same file, pigz -d -p $jobs" 0 g.gz ".\pigz.exe -d -p $jobs -c g.gz > o.bin"
                                                           Line "  same file, rapidgzip -P $jobs" 0 g.gz "`"$rg`" -d -P $jobs -c g.gz > o.bin"
$ct = C ".\pigz.exe -6 -p $jobs -c `"$f`" > p.gz";         Line "pigz -6 -p $jobs (decode pigz)" $ct p.gz ".\pigz.exe -d -p $jobs -c p.gz > o.bin"
                                                           Line "  same file, rapidgzip -P $jobs" 0 p.gz "`"$rg`" -d -P $jobs -c p.gz > o.bin"
$ct = C ".\bgzip.exe -l 6 -@ $jobs -c `"$f`" > b.gz";      Line "bgzip -l 6 -@ $jobs (decode bgzip -@ $jobs)" $ct b.gz ".\bgzip.exe -d -@ $jobs -c b.gz > o.bin"
                                                           Line "  same file, rapidgzip -P $jobs" 0 b.gz "`"$rg`" -d -P $jobs -c b.gz > o.bin"
$ct = C ".\hs_wrap15.exe c `"$f`" h.hsw --comp `"gzip.exe -6 -c {in}`" --jobs $jobs --block-mb 16 --raw off"
                                                           Line "hs_wrap 1.5 + gzip -6, Sets, 16 MB" $ct h.hsw ".\hs_wrap15.exe d h.hsw o.bin --decomp `"gzip.exe -d -c`" --jobs $jobs"
$ct = C ".\hs_wrap15.exe c `"$f`" k.hsw --comp `"gzip.exe -6 -c {in}`" --jobs $jobs --block-mb 16 --raw off --sets 0"
                                                           Line "hs_wrap 1.5 + gzip -6, plain blocks, 16 MB" $ct k.hsw ".\hs_wrap15.exe d k.hsw o.bin --decomp `"gzip.exe -d -c`" --jobs $jobs"
Remove-Item g.gz, p.gz, b.gz, h.hsw, k.hsw, o.bin, err.txt -ErrorAction SilentlyContinue
"FINISHED $(Get-Date)"
