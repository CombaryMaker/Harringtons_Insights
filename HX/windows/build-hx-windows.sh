#!/bin/bash
# build-hx-windows.sh - builds hx.exe (native Windows, x64) from the HX source with the llvm-mingw cross compiler.
# Michael Hugh Harrington, Nov 2026. Patent Pending (U.S. Provisional App. 64/192,843). Same license as HX.
# Needs: llvm-mingw (github.com/mstorsjo/llvm-mingw), and the public libraries listed in HX/BUILD.txt under ./libs
set -e
cd "$(dirname "$0")"
TC=${TC:-$PWD/llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64/bin}
CC=$TC/x86_64-w64-mingw32-clang; CXX=$TC/x86_64-w64-mingw32-clang++; NM=$TC/x86_64-w64-mingw32-nm; OC=$TC/x86_64-w64-mingw32-objcopy
L=$PWD/libs; O=$PWD/obj; mkdir -p $O/fse $O/fx $O/lib
OPT="-O2 -march=x86-64-v2 -DNDEBUG"

# 1. FSE / Huff0 (standalone), symbols renamed so they do not clash with zstd's own copy
( cd $O/fse && for f in entropy_common fse_compress fse_decompress huf_compress huf_decompress hist debug; do
    [ -f $L/FiniteStateEntropy/lib/$f.c ] && $CC $OPT -c $L/FiniteStateEntropy/lib/$f.c -o $f.o; done )
$NM -g --defined-only $O/fse/*.o | awk 'NF==3{print $3}' | sort -u | awk '{print $1, "fx_"$1}' > $O/redefs.txt
for o in $O/fse/*.o; do $OC --redefine-syms=$O/redefs.txt $o $O/fx/$(basename $o); done

# 2. zstd
( cd $L/zstd/lib && make -s clean >/dev/null 2>&1 || true; CC=$CC AR=$TC/x86_64-w64-mingw32-ar make -s -j8 libzstd.a ZSTD_LEGACY_SUPPORT=0 HAVE_PTHREAD=1 MOREFLAGS="$OPT" >/dev/null )

# 3. other libraries
cd $O/lib
for f in Alloc Bcj2 Bcj2Enc CpuArch LzFind LzFindMt LzFindOpt LzmaDec LzmaEnc Ppmd7 Ppmd7Dec Ppmd7Enc Ppmd8 Ppmd8Dec Ppmd8Enc Threads; do
  $CC $OPT -c $L/7zip/C/$f.c -o 7z_$f.o; done
$CC $OPT -I$L/bzip3/include -DVERSION=\"1.5.1\" -c $L/bzip3/src/libbz3.c -o libbz3.o
for f in $(find $L/brotli/c/common $L/brotli/c/enc $L/brotli/c/dec -name '*.c'); do
  $CC $OPT -I$L/brotli/c/include -c $f -o br_$(basename $(dirname $f))_$(basename $f .c).o; done
for f in $(find $L/libbsc/libbsc -name '*.cpp' -o -name 'libsais.c' | grep -v cubwt); do
  case $f in *.c) $CC $OPT -I$L/libbsc -c $f -o bsc_$(basename $f .c).o;; *) $CXX $OPT -I$L/libbsc -c $f -o bsc_$(basename $f .cpp).o;; esac; done
cd ../..

# 4. HX
$CXX $OPT -D_USE_MATH_DEFINES -std=c++17 -c src/hx.cpp -o $O/hx.o -I$L/FiniteStateEntropy/lib -I$L/ryg_rans -I$L/libbsc/libbsc -I$L/libbsc \
  -I$L/bzip3/include -I$L/zstd/lib -I$L/zstd/lib/common -I$L/brotli/c/include -I$L/7zip/C -Isrc
$OC --redefine-syms=$O/redefs.txt $O/hx.o $O/hx_r.o
mkdir -p dist
$CXX -O2 -static -o dist/hx.exe $O/hx_r.o $O/fx/*.o $O/lib/*.o $L/zstd/lib/libzstd.a -lpthread
ls -la dist/hx.exe
