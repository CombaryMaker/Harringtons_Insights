# Builds two 4 GB test files from public sources only (nothing from Michael's own files):
#   silesia_4gb.bin  = the 12 Silesia files joined, repeated 19 times (4,026,833,020 bytes)
#   mixed_4gb.tar    = a tar of four public downloads, extracted: Linux 6.6 kernel source (C/text), Blender 4.2.3 Windows
#                      portable (executables, DLLs, Python, fonts, images), pdf.js repository (JavaScript + ~1,000 test PDFs),
#                      NYT covid-19-data repository (CSV). File order inside the tar is the archive's own directory order.
$ErrorActionPreference = "Stop"; Set-Location $PSScriptRoot
$dc = Join-Path $HOME "Documents\data compression\HCM-tests"
if (!(Test-Path silesia_4gb.bin)) {
  $s = Join-Path $dc "v36-cores-silesia-2026-10-30"; $m = Join-Path $dc "v36-cores-2026-10-30"
  $parts = @("$s\dickens","$m\mozilla","$s\mr","$s\nci","$s\ooffice","$s\osdb","$s\reymont","$m\samba","$s\sao","$s\webster","$s\x-ray","$s\xml")
  $out = [System.IO.File]::Create("$PWD\silesia_4gb.bin")
  for ($i = 0; $i -lt 19; $i++) { foreach ($p in $parts) { $b = [System.IO.File]::ReadAllBytes($p); $out.Write($b, 0, $b.Length) } }
  $out.Close(); "silesia_4gb.bin built: $((Get-Item silesia_4gb.bin).Length) bytes"
}
if (Test-Path stage) { Remove-Item stage -Recurse -Force }
if (!(Test-Path mixed_4gb.tar)) {
  New-Item -ItemType Directory -Force dl, stage | Out-Null
  $srcs = @(
    @("linux-6.6.tar.gz",   "https://github.com/torvalds/linux/archive/refs/tags/v6.6.tar.gz"),
    @("blender-4.2.3.zip",  "https://download.blender.org/release/Blender4.2/blender-4.2.3-windows-x64.zip"),
    @("pdfjs-master.zip",   "https://github.com/mozilla/pdf.js/archive/refs/heads/master.zip"),
    @("covid-19-data.zip",  "https://github.com/nytimes/covid-19-data/archive/refs/heads/master.zip"))
  "SOURCES (download time $(Get-Date -Format u))"
  foreach ($x in $srcs) { $f = "dl\$($x[0])"
    if (!(Test-Path $f)) { & curl.exe -L -sS -o $f $x[1]; if ($LASTEXITCODE) { "  SKIPPED (download failed): $($x[1])"; Remove-Item $f -ErrorAction SilentlyContinue; continue } }
    "  $($x[0])  $((Get-Item $f).Length) bytes  sha256 $((Get-FileHash $f -Algorithm SHA256).Hash)  $($x[1])"
    $d = "stage\" + ($x[0] -replace "\.(tar\.gz|zip)$",""); New-Item -ItemType Directory -Force $d | Out-Null
    & tar.exe -xf $f -C $d; if ($LASTEXITCODE) { "  WARNING: extract reported errors for $f" } }
  & tar.exe -cf mixed_4gb.tar -C stage .; if ($LASTEXITCODE) { throw "tar failed" }
  "mixed_4gb.tar built: $((Get-Item mixed_4gb.tar).Length) bytes"
  Remove-Item stage -Recurse -Force
}
