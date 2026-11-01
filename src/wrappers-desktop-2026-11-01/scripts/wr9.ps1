# wr9.ps1 - desktop test of hs_build 1.1 on a project with many heavy files, 2026-11-01. Michael Hugh Harrington, MIT License.
# Project: the public nlohmann/json 3.12.0 unit tests (69 template-heavy C++ files), plain one-rule Makefile in file
# name order, flags from the project's tests/CMakeLists.txt, gcc -O2 -std=c++11 (w64devkit 2.4.0, already unpacked
# here by wr7/wr8). Same compiler, same flags; only the start order changes. Objects compared byte for byte.
$ErrorActionPreference = 'Continue'
$D = $PSScriptRoot
Set-Location $D
$L = Join-Path $D 'wr9_log.txt'
function Log($s) { Add-Content -Path $L -Value $s -Encoding UTF8 }
Log "hs_build big-project desktop test started $(Get-Date)"
Log "CPU: $((Get-CimInstance Win32_Processor).Name), threads $([Environment]::ProcessorCount)"
$h = (Get-FileHash (Join-Path $D 'projjson.zip') -Algorithm SHA256).Hash
if ($h -ne '35D80EEFC41B678A2B6EB9B33CD2E861EBFB34AFDBFCEBFB5318AEB403F5119D') { Log "STOP: projjson.zip hash $h"; Log 'DONE'; exit }
foreach ($f in @("$D\w64devkit\bin\g++.exe", "$D\w64devkit\bin\make.exe", "$D\hsb\hs_build.exe", "$D\hsb\hs_cc.exe")) { if (-not (Test-Path $f)) { Log "STOP: missing $f"; Log 'DONE'; exit } }
Log 'verified: projjson.zip is the intended file; w64devkit, hs_build.exe and hs_cc.exe (built from source by wr8) present'
$P = "$D\projjson"
if (-not (Test-Path "$P\Makefile")) { New-Item -ItemType Directory -Force $P | Out-Null; & "$env:SystemRoot\System32\tar.exe" -xf projjson.zip -C $P }
if (-not (Test-Path "$P\Makefile")) { Log 'STOP: project did not unpack'; Log 'DONE'; exit }
$env:PATH = "$D\w64devkit\bin;$D\hsb;" + $env:PATH
function RunP($exe, $argstr, $tag) {
    $e = Join-Path $D "err9_$tag.txt"; $o = Join-Path $D "out9_$tag.txt"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $argstr -WorkingDirectory $P -RedirectStandardError $e -RedirectStandardOutput $o -NoNewWindow -Wait -PassThru
    $sw.Stop()
    return [math]::Round($sw.Elapsed.TotalSeconds, 1)
}
function Clean { if (Test-Path "$P\obj") { Remove-Item -Recurse -Force "$P\obj" } }
function ObjHash { $s = ''; if (Test-Path "$P\obj") { Get-ChildItem "$P\obj" -File -Filter *.o | Sort-Object Name | ForEach-Object { $s += $_.Name + (Get-FileHash $_.FullName -Algorithm MD5).Hash } }; return $s }
function NObj { if (Test-Path "$P\obj") { return (Get-ChildItem "$P\obj" -File -Filter *.o).Count } else { return 0 } }
function LastLine($tag, $pat) { $e = Join-Path $D "err9_$tag.txt"; if (Test-Path $e) { $m = Select-String -Path $e -Pattern $pat | Select-Object -Last 1; if ($m) { return $m.Line.Trim() } }; return '' }
$make = "$D\w64devkit\bin\make.exe"; $hsb = "$D\hsb\hs_build.exe"; $prof = "$D\hs_profile_json.txt"
Log ''
Log '=== nlohmann/json 3.12.0 unit tests, 69 compile jobs, g++ -O2 -std=c++11, 12 jobs (wall seconds; objects compared with plain run 1)'
Clean; $t = RunP $make '-j12 all' 'plain1'; $ref = ObjHash; Log ("plain make -j12 run 1:              {0} s   ({1} objects)" -f $t, (NObj))
Clean; $t = RunP $make '-j12 all' 'plain2'; Log ("plain make -j12 run 2:              {0} s   objects same: {1}" -f $t, ((ObjHash) -eq $ref))
Clean; Remove-Item $prof -ErrorAction SilentlyContinue
$env:HS_CC_LOG = $prof
$t = RunP $make '-j12 all "CXX=hs_cc g++"' 'shim1'
Log ("plain make -j12 with timing shim:   {0} s   objects same: {1}   (this run writes the profile)" -f $t, ((ObjHash) -eq $ref))
Copy-Item $prof "$D\hs_profile_json_plain.txt" -ErrorAction SilentlyContinue
$env:HS_CC_LOG = $null
Clean; $tmpprof = "$D\hs_profile_json_size.txt"; Remove-Item $tmpprof -ErrorAction SilentlyContinue
$t = RunP $hsb "--jobs 12 --profile `"$tmpprof`" -- all" 'size1'
Log ("hs_build, source-size estimate:     {0} s   objects same: {1}   [{2}]" -f $t, ((ObjHash) -eq $ref), (LastLine 'size1' 'heaviest|error'))
foreach ($r in 1..2) {
    Clean
    $t = RunP $hsb "--jobs 12 --profile `"$prof`" -- all" "prof$r"
    Log ("hs_build, measured profile {0}:       {1} s   objects same: {2}   [{3}]" -f $r, $t, ((ObjHash) -eq $ref), (LastLine "prof$r" 'heaviest|error'))
}
Clean; $t = RunP $make '-j12 all' 'plain3'; Log ("plain make -j12 run 3:              {0} s   objects same: {1}" -f $t, ((ObjHash) -eq $ref))
Log ''
Log 'measured compile times from the plain shim run (seconds): heaviest 10, and where they sit in Makefile order (of 69)'
if (Test-Path "$D\hs_profile_json_plain.txt") {
    $rows = Get-Content "$D\hs_profile_json_plain.txt" | ForEach-Object { $f = $_ -split "`t"; [pscustomobject]@{ s = [double]$f[0]; d = [double]$f[1]; o = $f[2] } }
    $order = $rows | Sort-Object s; $i = 0; $pos = @{}; foreach ($x in $order) { $i++; $pos[$x.o] = $i }
    $tot = ($rows | Measure-Object d -Sum).Sum
    Log ("  total compile work {0:N0} CPU-seconds over {1} files" -f $tot, $rows.Count)
    $rows | Sort-Object d -Descending | Select-Object -First 10 | ForEach-Object { Log ("  {0,-40} {1,6:N1} s   started {2,2} of {3}" -f $_.o, $_.d, $pos[$_.o], $rows.Count) }
}
Log ''
Log "FINISHED $(Get-Date)"
Log 'DONE'
