# wr6.ps1 - rerun of the Tesseract and Tika parts of wr5.ps1 - desktop test of hs_gs (Ghostscript), hs_ocr (Tesseract) and HsTika (Apache Tika), 2026-11-01
# Michael Hugh Harrington, MIT License. Portable tools only (nothing installed); public test data only.
$ErrorActionPreference = 'Continue'
$D = $PSScriptRoot
Set-Location $D
$L = Join-Path $D 'wr6_log.txt'
function Log($s) { Add-Content -Path $L -Value $s -Encoding UTF8 }
Log "wrappers2 rerun (Tesseract, Tika) started $(Get-Date)"
$cpu = (Get-CimInstance Win32_Processor).Name
Log "CPU: $cpu, threads $([Environment]::ProcessorCount)"

# ---- rerun of the Tesseract and Tika parts with the missing ICU 79 files added (fix.zip)
$h = (Get-FileHash (Join-Path $D 'fix.zip') -Algorithm SHA256).Hash
if ($h -ne 'A5F8BA760F3D1A84AA244B8F15D17C9CEC5A2596721A94BCDBB4D2F6CD2B6249') { Log "STOP: fix.zip hash mismatch $h"; Log 'DONE'; exit }
& "$env:SystemRoot\System32\tar.exe" -xf fix.zip
$want = @{ 'hs\hs_ocr.exe' = '354481C681CAE130B18F6BE0BF8EAB7B4E1E7C70F574BD110AC7C2FCE46A28BF';
           'hs\hs_tika.jar' = '5E4B312AF4260F95D8B4F12D74099266B3B78B7377D36DF7FB8D40BB0276D370';
           'data\mixed.pdf' = '3600F2584BA8D97FF6171359D9CC03160D49A2313DBA1723AC8259FF6B0086E2';
           'data\clean_mixed.pdf' = 'ABF033FEDEA1B94C1CD42CAB7C7DD91689956A142C595747058AA5BE61D5584F';
           'tools\bin\icuuc79.dll' = '2029B7604D21C4F03139CA7C59726F2ACED8CB6E45665F3166D1AB9B5579D1C8' }
foreach ($k in $want.Keys) {
    $p = Join-Path $D $k
    if (-not (Test-Path $p)) { Log "STOP: missing $k"; Log 'DONE'; exit }
    $x = (Get-FileHash $p -Algorithm SHA256).Hash
    if ($x -ne $want[$k]) { Log "STOP: $k hash $x not the intended file"; Log 'DONE'; exit }
}
Log 'verified: fix.zip, icuuc79.dll, hs_ocr.exe, hs_tika.jar, mixed.pdf, clean_mixed.pdf are the intended files'

$T = Join-Path $D 'tools'
$env:PATH = "$T\bin;$D\jre\bin;" + $env:PATH
$env:TESSDATA_PREFIX = "$T\tessdata"
$gs = "$T\bin\gswin64c.exe"; $tess = "$T\bin\tesseract.exe"; $java = "$D\jre\bin\java.exe"

function RunP($exe, $argstr, $tag) {
    $e = Join-Path $D "err_$tag.txt"; $o = Join-Path $D "out_$tag.txt"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $argstr -RedirectStandardError $e -RedirectStandardOutput $o -WorkingDirectory $D -NoNewWindow -Wait -PassThru
    $sw.Stop()
    return [math]::Round($sw.Elapsed.TotalSeconds, 1)
}
function LastLine($tag, $pat) {
    $e = Join-Path $D "err_$tag.txt"
    if (Test-Path $e) { $m = Select-String -Path $e -Pattern $pat | Select-Object -Last 1; if ($m) { return $m.Line.Trim() } }
    return ''
}
function FirstLines($tag) {
    $r = ''
    foreach ($f in @("out_$tag.txt", "err_$tag.txt")) { $p = Join-Path $D $f; if (Test-Path $p) { $r += ((Get-Content $p -TotalCount 3) -join ' | ') + ' ' } }
    return $r.Trim()
}
$null = RunP $gs '--version' 'v_gs'; $null = RunP $tess '--version' 'v_tess'; $null = RunP $java '-version' 'v_java'
Log "Ghostscript: $(FirstLines 'v_gs')"
Log "Tesseract: $(FirstLines 'v_tess')"
Log "Java: $(FirstLines 'v_java')"
if ((FirstLines 'v_tess') -eq '') { Log 'STOP: Tesseract still does not start'; Log 'DONE'; exit }

# ================= Tesseract: 180 generated page images (100 normal, 40 nearly blank, 40 dense at the end)
Log ''
Log '=== Tesseract on 180 page images (from public Silesia dickens), English, 12 jobs'
$lst = Join-Path $D 'ocr_list.txt'
$paths = Get-ChildItem "$D\data\pages" -File -Filter *.png | Sort-Object Name | ForEach-Object { $_.FullName }
[IO.File]::WriteAllText($lst, (($paths -join "`n") + "`n"))
Remove-Item Env:OMP_NUM_THREADS -ErrorAction SilentlyContinue; Remove-Item Env:OMP_THREAD_LIMIT -ErrorAction SilentlyContinue
$t = RunP $tess "`"$lst`" `"$D\ocr_plain`" -l eng" 'ocr_plain'
$len = if (Test-Path "$D\ocr_plain.txt") { (Get-Item "$D\ocr_plain.txt").Length } else { 0 }
Log "plain Tesseract, its own threads:       $t s   ($len text bytes)"
$env:OMP_NUM_THREADS = '1'; $env:OMP_THREAD_LIMIT = '1'
$t = RunP $tess "`"$lst`" `"$D\ocr_plain1`" -l eng" 'ocr_plain1'
$same = if ((Test-Path "$D\ocr_plain1.txt") -and (Test-Path "$D\ocr_plain.txt")) { (Get-FileHash "$D\ocr_plain1.txt").Hash -eq (Get-FileHash "$D\ocr_plain.txt").Hash } else { $false }
Log "plain Tesseract, 1 thread:              $t s   (same text: $same)"
foreach ($sp in @('equal', 'guided')) {
    $t = RunP "$D\hs\hs_ocr.exe" "`"$lst`" `"$D\ocr_$sp`" --jobs 12 --tesseract `"$tess`" --split $sp -- -l eng" "ocr_$sp"
    $same = if (Test-Path "$D\ocr_$sp.txt") { (Get-FileHash "$D\ocr_$sp.txt").Hash -eq (Get-FileHash "$D\ocr_plain.txt").Hash } else { $false }
    Log ("hs_ocr {0,-6}:                          {1} s   (same text: {2})   [{3}]" -f $sp, $t, $same, (LastLine "ocr_$sp" 'hs_ocr:|error'))
}
Remove-Item Env:OMP_NUM_THREADS -ErrorAction SilentlyContinue; Remove-Item Env:OMP_THREAD_LIMIT -ErrorAction SilentlyContinue

# ================= Tika (with Tesseract OCR on PATH)
Log ''
Log '=== Apache Tika 3.3.3 + Tesseract OCR, body text, 12 jobs'
$cp = "tools\tika-app-3.3.3.jar;hs\hs_tika.jar"   # relative to the test folder (absolute quoted path failed on Windows)
foreach ($f in @('clean_mixed', 'mixed')) {
    $in = "$D\data\$f.pdf"
    $t = RunP $java "-cp $cp HsTika `"$in`" `"$D\tika_${f}_plain.txt`" --plain" "tk_${f}_plain"
    $len = if (Test-Path "$D\tika_${f}_plain.txt") { (Get-Item "$D\tika_${f}_plain.txt").Length } else { 0 }
    Log ("{0}: plain Tika        {1} s   ({2} text bytes)   [{3}]" -f $f, $t, $len, (LastLine "tk_${f}_plain" 'plain Tika|Exception'))
    foreach ($sp in @('equal', 'guided')) {
        $t = RunP $java "-cp $cp HsTika `"$in`" `"$D\tika_${f}_$sp.txt`" --jobs 12 --split $sp" "tk_${f}_$sp"
        $same = if (Test-Path "$D\tika_${f}_$sp.txt") { (Get-FileHash "$D\tika_${f}_$sp.txt").Hash -eq (Get-FileHash "$D\tika_${f}_plain.txt").Hash } else { $false }
        Log ("{0}: HsTika {1,-6}     {2} s   (same text: {3})   [{4}]" -f $f, $sp, $t, $same, (LastLine "tk_${f}_$sp" 'HsTika:|Exception'))
    }
}
Log ''
Log "FINISHED $(Get-Date)"
Log 'DONE'
