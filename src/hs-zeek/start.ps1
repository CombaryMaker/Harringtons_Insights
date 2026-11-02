# start.ps1 - Zeek exact-match desktop test with Harrington's Sets, all cores. Michael Hugh Harrington, 2026. MIT License.
# 1) joins and checks the test bundle, 2) downloads Ubuntu's official WSL image and checks its published SHA-256,
# 3) imports it as its own WSL Linux named ZeekTest, stored in this folder (removed after the test),
# 4) runs go.sh inside it. Everything the test makes stays in this folder; results go to results.txt.
$ErrorActionPreference = 'Continue'
$D = $PSScriptRoot; Set-Location $D
$L = Join-Path $D 'setup_log.txt'
function Log($s) { Add-Content -Path $L -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $s" -Encoding UTF8 }
Log 'setup started'
cmd /c "copy /b zk.tar.xz.part00+zk.tar.xz.part01+zk.tar.xz.part02+zk.tar.xz.part03 zk.tar.xz >NUL"
$h = (Get-FileHash "$D\zk.tar.xz" -Algorithm SHA256).Hash
if ($h -ne '1B14E38A1F02848A4A5DDA60DF1ED7DB54027E9F7EAAC6C949D59ECBDA7C280A') { Log "STOP: bundle hash $h"; Log 'DONE'; exit }
Log 'bundle verified (SHA-256 matches the cloud-tested package)'
$img = Join-Path $D 'ubuntu-noble-wsl-amd64-wsl.rootfs.tar.gz'
if (!(Test-Path $img)) {
  Log 'downloading Ubuntu 24.04 WSL image from cloud-images.ubuntu.com (357 MB)'
  & "$env:SystemRoot\System32\curl.exe" -L -s -o $img 'https://cloud-images.ubuntu.com/wsl/releases/24.04/current/ubuntu-noble-wsl-amd64-wsl.rootfs.tar.gz'
}
$h = (Get-FileHash $img -Algorithm SHA256).Hash
if ($h -ne '8251E27FFFF381A4AF5F41DCB94D867DE3E0D9774A9241908AB34555D99315EA') { Log "STOP: Ubuntu image hash $h"; Log 'DONE'; exit }
Log 'Ubuntu image verified (matches Ubuntu''s published SHA-256)'
$wsl = "$env:SystemRoot\System32\wsl.exe"
$names = ((& $wsl --list --quiet) -join "`n") -replace "`0", ''
if ($names -notmatch '(?m)^ZeekTest\s*$') {
  & $wsl --import ZeekTest "$D\wsl" $img --version 2 *> "$D\wsl_import_log.txt"
  if ($LASTEXITCODE -ne 0) {
    Log "WSL 2 import failed (code $LASTEXITCODE), trying WSL 1"
    & $wsl --import ZeekTest "$D\wsl" $img --version 1 *>> "$D\wsl_import_log.txt"
    if ($LASTEXITCODE -ne 0) { Log "STOP: WSL import failed (code $LASTEXITCODE), see wsl_import_log.txt"; Log 'DONE'; exit }
    Log 'imported as WSL 1'
  } else { Log 'imported as WSL 2' }
} else { Log 'ZeekTest already present' }
Log 'starting test inside ZeekTest (progress in results.txt)'
& $wsl -d ZeekTest -u root --cd "$D" -e bash go.sh
Log "test finished (code $LASTEXITCODE)"
Log 'DONE'
