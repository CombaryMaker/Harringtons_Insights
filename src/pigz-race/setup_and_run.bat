@echo off
cd /d "%~dp0"
echo ==== %date% %time% setup start (v2: zig download, no Python) >> setup_log.txt
copy /y "..\gzip-direct-2026-10-31\gzip.exe" . >> setup_log.txt 2>&1
copy /y "..\lz4-tier-2026-10-31\hs_wrap14.exe" . >> setup_log.txt 2>&1
set ZIG=zig\zig-windows-x86_64-0.13.0\zig.exe
if not exist %ZIG% (
  echo downloading zig >> setup_log.txt
  curl.exe -L -sS -o zig.zip https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip >> setup_log.txt 2>&1
  powershell -NoProfile -Command "Expand-Archive -LiteralPath zig.zip -DestinationPath zig -Force" >> setup_log.txt 2>&1
  del zig.zip
)
if not exist %ZIG% (echo STOPPED: zig not available >> setup_log.txt & exit /b 1)
%ZIG% version >> setup_log.txt 2>&1
if not exist pigz.exe (
  echo building pigz >> setup_log.txt
  %ZIG% cc -target x86_64-windows-gnu -O2 -static -DNOZOPFLI -Isrc\shim -Isrc\zlib -o pigz.exe src\pigz\pigz.c src\pigz\yarn.c src\pigz\try.c src\zlib\adler32.c src\zlib\compress.c src\zlib\crc32.c src\zlib\deflate.c src\zlib\infback.c src\zlib\inffast.c src\zlib\inflate.c src\zlib\inftrees.c src\zlib\trees.c src\zlib\uncompr.c src\zlib\zutil.c src\zlib\gzclose.c src\zlib\gzlib.c src\zlib\gzread.c src\zlib\gzwrite.c >> setup_log.txt 2>&1
)
if not exist pigz.exe (echo STOPPED: pigz build failed, see setup_log.txt >> setup_log.txt & exit /b 1)
pigz.exe --version >> setup_log.txt 2>&1
if not exist pigzrace.exe (
  echo building harness >> setup_log.txt
  %ZIG% c++ -target x86_64-windows-gnu -O2 -std=c++17 -static pigzrace.cpp -o pigzrace.exe >> setup_log.txt 2>&1
)
if not exist pigzrace.exe (echo STOPPED: harness build failed >> setup_log.txt & exit /b 1)
echo ==== build ok, running test >> setup_log.txt
pigzrace.exe
echo ==== %date% %time% finished, exit %errorlevel% >> setup_log.txt
exit /b 0
