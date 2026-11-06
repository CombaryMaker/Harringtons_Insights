#!/bin/bash
# build-gui.sh - builds HX-Compressor.exe (the Windows window app) with llvm-mingw. Michael Hugh Harrington, Nov 2026.
set -e; cd "$(dirname "$0")"; TC=${TC:-$PWD/llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64/bin}
$TC/x86_64-w64-mingw32-windres hx_gui.rc -O coff -o hx_gui_res.o
$TC/x86_64-w64-mingw32-clang++ -O2 -std=c++17 -municode -mwindows -static hx_gui.cpp hx_gui_res.o -o HX-Compressor.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32
