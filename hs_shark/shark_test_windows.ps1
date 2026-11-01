# hs_shark desktop test: installs nothing itself (install_wireshark.bat does that), builds the public test captures,
# then races plain tshark against hs_shark classic and direct modes, 3 runs each, output checked line for line.
$ErrorActionPreference = "Continue"; Set-Location $PSScriptRoot
$jobs = 12; $runs = 3
$tshark = "C:\Program Files\Wireshark\tshark.exe"; $editcap = "C:\Program Files\Wireshark\editcap.exe"
if (!(Test-Path $tshark)) { Write-Output "tshark missing"; exit 1 } # "tshark.exe not found at $tshark - Wireshark install did not complete" }
& $tshark --version | Select-Object -First 1
Copy-Item (Join-Path $HOME "Documents\data compression\hs-shark-2026-10-30\hs_shark12.exe") . -Force
if (!(Test-Path big8.pcap)) {
  New-Item -ItemType Directory -Force dl, caps | Out-Null
  $srcs = @(@("packets.zip", "https://github.com/chrissanders/packets/archive/refs/heads/master.zip"),
            @("ics-pcap.zip", "https://github.com/automayt/ICS-pcap/archive/refs/heads/master.zip"))
  foreach ($x in $srcs) { $f = "dl\$($x[0])"
    if (!(Test-Path $f)) { & curl.exe -L -sS -o $f $x[1]; if ($LASTEXITCODE) { "SKIPPED: $($x[1])"; continue } }
    "  $($x[0])  $((Get-Item $f).Length) bytes  sha256 $((Get-FileHash $f -Algorithm SHA256).Hash)  $($x[1])"
    & tar.exe -xf $f -C caps 2>$null }
  # convert every capture (pcapng or pcap, any link type) to classic Ethernet pcap with Wireshark's editcap; LFS placeholders fail and are skipped
  New-Item -ItemType Directory -Force conv | Out-Null; $k = 0; $skipped = 0; $ErrorActionPreference = "SilentlyContinue"
  foreach ($c in (Get-ChildItem caps -Recurse -File | Where-Object { $_.Extension -match '^\.(pcap|pcapng|cap)$' })) {
    $k++; & $editcap -F pcap $c.FullName ("conv\c{0:D4}.pcap" -f $k) 2>$null; if ($LASTEXITCODE) { $skipped++; Remove-Item ("conv\c{0:D4}.pcap" -f $k) -ErrorAction SilentlyContinue } }
  "editcap: $k capture files found, $skipped unreadable (Git LFS placeholders); bigcap then keeps Ethernet captures only"
  & .\bigcap.exe conv base.pcap big8.pcap
  cmd /c "rd /s /q caps conv" 2>$null
}
"big8.pcap: $((Get-Item big8.pcap).Length) bytes  sha256 $((Get-FileHash big8.pcap -Algorithm SHA256).Hash)"
$fields = @("-T", "fields", "-e", "frame.number", "-e", "frame.time_relative", "-e", "ip.src", "-e", "ip.dst", "-e", "tcp.stream", "-e", "_ws.col.Protocol", "-e", "_ws.col.Info")
function T($exe, $a, $outfile) { $sw = [Diagnostics.Stopwatch]::StartNew(); & $exe @a > $outfile 2> err.txt; $sw.Stop(); if ($LASTEXITCODE) { throw "$exe failed: $(Get-Content err.txt -Raw)" }; return $sw.Elapsed.TotalSeconds }
""; "hs_shark test, desktop, $(Get-Date), jobs=$jobs, runs=$runs"
"CPU: $((Get-CimInstance Win32_Processor).Name), threads $((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)"
# plain tshark via cmd so stdout is written as raw bytes (a PowerShell redirect re-encodes it as UTF-16, which broke the earlier compare)
$fq = ($fields | ForEach-Object { if ($_ -match '\.') { $_ } else { $_ } }) -join " "
$pl = @(); for ($r = 0; $r -lt $runs; $r++) { $sw = [Diagnostics.Stopwatch]::StartNew(); cmd /c "`"$tshark`" -r big8.pcap $fq > plain.txt 2> err.txt"; $sw.Stop(); $pl += $sw.Elapsed.TotalSeconds }
function NormHash($f) { $t = [IO.File]::ReadAllText($f) -replace "`r`n", "`n"; $sha = [Security.Cryptography.SHA256]::Create(); return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($t))) }
"plain tshark (one core):      " + (($pl | ForEach-Object { '{0:N1}' -f $_ }) -join " / ") + " s"
$ref = NormHash plain.txt; "plain output: $((Get-Item plain.txt).Length) bytes, $((Get-Content plain.txt | Measure-Object -Line).Lines) lines"
foreach ($mode in @("classic", "direct")) {
  $ts = @()
  for ($r = 0; $r -lt $runs; $r++) { $ts += T .\hs_shark12.exe (@("big8.pcap", "--jobs", "$jobs", "--tshark", $tshark, "--editcap", $editcap, "--out", "hs_$mode.txt", "--mode", $mode, "--") + $fields) hs_$mode.log }
  $same = ((NormHash "hs_$mode.txt") -eq $ref); $hb = (Get-Item "hs_$mode.txt").Length
  "hs_shark --mode $mode ($jobs jobs): " + (($ts | ForEach-Object { '{0:N1}' -f $_ }) -join " / ") + " s   output $hb bytes, identical to plain (line endings normalised): $same"
}
Remove-Item plain.txt, hs_classic.txt, hs_direct.txt, err.txt -ErrorAction SilentlyContinue
""; "FINISHED $(Get-Date)"
