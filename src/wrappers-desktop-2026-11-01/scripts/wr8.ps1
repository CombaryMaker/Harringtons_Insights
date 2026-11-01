# wr7.ps1 - desktop test of hs_build 1.1 (Harrington Sets build scheduler), 2026-11-01. Michael Hugh Harrington, MIT License.
# Project: the public zstd 1.5.6 library sources (25 C files) with a plain one-rule Makefile, built by w64devkit 2.4.0
# (portable gcc + GNU make + busybox sh, nothing installed). Same compiler, same flags; only the start order changes.
$ErrorActionPreference = 'Continue'
$D = $PSScriptRoot
Set-Location $D
$L = Join-Path $D 'wr8_log.txt'
function Log($s) { Add-Content -Path $L -Value $s -Encoding UTF8 }
Log "hs_build desktop test (rerun, tools built from source on this PC) started $(Get-Date)"
Log "CPU: $((Get-CimInstance Win32_Processor).Name), threads $([Environment]::ProcessorCount)"
if (-not (Test-Path "$D\w64devkit-x64-2.4.0.7z.exe")) { cmd /c "copy /b w64dk.part1+w64dk.part2 w64devkit-x64-2.4.0.7z.exe >NUL" }
$want = @{ 'w64devkit-x64-2.4.0.7z.exe' = '80678C29A56982D7D3B342F12A0060157DF5654C38D82D4ADC40704070D6AAFB';
           'proj.zip' = '04D31CC1DD8D0FA33BE547C98689EDCA6D44530783559E7977C0B373F9807F26';
           'hsb\hs_build.cpp' = 'A81D1E4324D634297474AD1CC55DDF1C31D30D8260BF2046E29CB736E04662A6';
           'hsb\hs_cc.cpp' = '11A3DB77748D4A73F66062F98C502271EE01F63B5223DE67B83A921C8C23CA99' }
foreach ($k in $want.Keys) {
    $p = Join-Path $D $k
    if (-not (Test-Path $p)) { Log "STOP: missing $k"; Log 'DONE'; exit }
    $x = (Get-FileHash $p -Algorithm SHA256).Hash
    if ($x -ne $want[$k]) { Log "STOP: $k hash $x not the intended file"; Log 'DONE'; exit }
}
Log 'verified: w64devkit 2.4.0, proj.zip, hs_build.cpp 1.1, hs_cc.cpp 1.1 are the intended files'
if (-not (Test-Path "$D\w64devkit\bin\gcc.exe")) {
    & "$env:SystemRoot\System32\tar.exe" -xf w64devkit-x64-2.4.0.7z.exe 2>$null
    if (-not (Test-Path "$D\w64devkit\bin\gcc.exe")) {
        $p = Start-Process -FilePath "$D\w64devkit-x64-2.4.0.7z.exe" -ArgumentList "-o`"$D`" -y" -Wait -PassThru
    }
}
if (-not (Test-Path "$D\w64devkit\bin\gcc.exe")) { Log 'STOP: could not unpack w64devkit'; Log 'DONE'; exit }
if (-not (Test-Path "$D\proj\Makefile")) { New-Item -ItemType Directory -Force "$D\proj" | Out-Null; & "$env:SystemRoot\System32\tar.exe" -xf proj.zip -C "$D\proj" }
if (-not (Test-Path "$D\proj\Makefile")) { Log 'STOP: project did not unpack'; Log 'DONE'; exit }
$env:PATH = "$D\w64devkit\bin;$D\hsb;" + $env:PATH
$P = "$D\proj"
# build hs_build and hs_cc here from their source with the toolkit's own g++ (the earlier prebuilt hs_cc.exe was
# quarantined by Windows Defender as a false positive)
Remove-Item "$D\hsb\hs_build.exe","$D\hsb\hs_cc.exe" -ErrorAction SilentlyContinue
& "$D\w64devkit\bin\g++.exe" -O2 -std=c++17 -static "$D\hsb\hs_build.cpp" -o "$D\hsb\hs_build.exe" 2> "$D\err_build_hsb.txt"
& "$D\w64devkit\bin\g++.exe" -O2 -std=c++17 -static "$D\hsb\hs_cc.cpp" -o "$D\hsb\hs_cc.exe" 2> "$D\err_build_hscc.txt"
if (-not ((Test-Path "$D\hsb\hs_build.exe") -and (Test-Path "$D\hsb\hs_cc.exe"))) { Log 'STOP: could not build hs_build/hs_cc from source'; Log 'DONE'; exit }
Log 'built hs_build.exe and hs_cc.exe from source on this PC with w64devkit g++'
function RunP($exe, $argstr, $tag) {
    $e = Join-Path $D "err_$tag.txt"; $o = Join-Path $D "out_$tag.txt"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $argstr -WorkingDirectory $P -RedirectStandardError $e -RedirectStandardOutput $o -NoNewWindow -Wait -PassThru
    $sw.Stop()
    return [math]::Round($sw.Elapsed.TotalSeconds, 2)
}
function Clean { Get-ChildItem $P -File | Where-Object { $_.Extension -eq '.o' -or $_.Name -eq 'libz.a' } | Remove-Item -Force }
function ObjHash { $s = ''; Get-ChildItem $P -File -Filter *.o | Sort-Object Name | ForEach-Object { $s += $_.Name + (Get-FileHash $_.FullName -Algorithm MD5).Hash }; return $s }
function LastLine($tag, $pat) { $e = Join-Path $D "err_$tag.txt"; if (Test-Path $e) { $m = Select-String -Path $e -Pattern $pat | Select-Object -Last 1; if ($m) { return $m.Line.Trim() } }; return '' }
$null = RunP "$D\w64devkit\bin\gcc.exe" '--version' 'v_gcc'; $null = RunP "$D\w64devkit\bin\make.exe" '--version' 'v_make'
Log "gcc: $((Get-Content "$D\out_v_gcc.txt" -TotalCount 1))   make: $((Get-Content "$D\out_v_make.txt" -TotalCount 1))"
$make = "$D\w64devkit\bin\make.exe"; $hsb = "$D\hsb\hs_build.exe"; $prof = "$D\hs_profile.txt"
Log ''
Log '=== zstd 1.5.6 library, 25 compile jobs, gcc -O2, 12 jobs (wall seconds; objects compared with the first plain build)'
Clean; $t = RunP $make '-j12 all' 'mk_plain0'
$ref = ObjHash
$n = (Get-ChildItem $P -File -Filter *.o).Count
Log "plain make -j12 (warm-up):            $t s   ($n objects)"
foreach ($r in 1..3) { Clean; $t = RunP $make '-j12 all' "mk_plain$r"; Log ("plain make -j12 run {0}:                {1} s   objects same: {2}" -f $r, $t, ((ObjHash) -eq $ref)) }
foreach ($r in 1..2) { Clean; $t = RunP $make '-j12 all "CC=hs_cc gcc"' "mk_shim$r"; Log ("plain make -j12 with timing shim {0}:   {1} s   objects same: {2}" -f $r, $t, ((ObjHash) -eq $ref)) }
foreach ($r in 1..2) {
    Clean; Remove-Item $prof -ErrorAction SilentlyContinue
    $t = RunP $hsb "--jobs 12 --profile `"$prof`" -- all" "hsb_size$r"
    Log ("hs_build, source-size estimate {0}:     {1} s   objects same: {2}   [{3}]" -f $r, $t, ((ObjHash) -eq $ref), (LastLine "hsb_size$r" 'heaviest|error'))
}
foreach ($r in 1..3) {
    Clean
    $t = RunP $hsb "--jobs 12 --profile `"$prof`" -- all" "hsb_prof$r"
    Log ("hs_build, measured profile {0}:         {1} s   objects same: {2}   [{3}]" -f $r, $t, ((ObjHash) -eq $ref), (LastLine "hsb_prof$r" 'heaviest|error'))
}
foreach ($r in 4..5) { Clean; $t = RunP $make '-j12 all' "mk_plain$r"; Log ("plain make -j12 run {0}:                {1} s   objects same: {2}" -f $r, $t, ((ObjHash) -eq $ref)) }
Log ''
Log 'measured compile times (seconds, from the profile), heaviest 8:'
if (Test-Path $prof) { Get-Content $prof | ForEach-Object { $f = $_ -split "`t"; [pscustomobject]@{ d = [double]$f[1]; o = $f[2] } } | Group-Object o | ForEach-Object { [pscustomobject]@{ o = $_.Name; d = ($_.Group | Measure-Object d -Maximum).Maximum } } | Sort-Object d -Descending | Select-Object -First 8 | ForEach-Object { Log ("  {0,-28} {1:N2}" -f $_.o, $_.d) } }
Log ''
Log "FINISHED $(Get-Date)"
Log 'DONE'
