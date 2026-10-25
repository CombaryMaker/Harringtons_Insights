# Windows programs

Drag a file onto a program's icon to compress it (it is checked by decoding it
back before it is saved). Drag the compressed file onto the same program to
restore it.

| Program | Makes | Notes |
|---|---|---|
| `HCM.exe` | `.hcm` | HCM v3.3, fast mode |
| `HCM-Max.exe` | `.hcm` | HCM v3.3, max mode (same program; the name picks the mode) |
| `bzip2-hi.exe` | `.bz2` | standard .bz2; any bzip2 or 7-Zip opens it |
| `xz-hi.exe` | `.xz` | standard .xz; any xz or 7-Zip opens it |

Compression takes longer than the originals (the encoders try many options;
xz-hi on a 1 MB binary file can take a couple of minutes). Decoding does not.

## Building (cross-compiled with clang + mingw-w64)

- `xz-hi.exe`: `xz_hi.cpp` + `dropframe.h`, linked with liblzma built from
  XZ Utils 5.4.5 (`src/liblzma`, single-threaded, using `config.h` here).
  liblzma is in the public domain (XZ Utils 5.4.x).
- `bzip2-hi.exe`: `bzip2_hi.cpp` + `dropframe.h`, linked with the library files
  of `../bzip2-hi/source` (altered bzip2; bzip2 license applies).
- `HCM.exe`: `../hcm/hcm.cpp`, built with `-O2 -ffp-contract=off`.
