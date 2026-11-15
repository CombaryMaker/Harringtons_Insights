STL-HARRINGTON - lossless STL compressor, first prototype (0.1)
Michael Hugh Harrington, 15 November 2026. MIT License.
Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.

WHAT IT DOES
Packs STL and OBJ files (the shapes sent to 3D printers) into much smaller .stlh files, and restores .stlh files back to STL.
We tried it on 26 everyday print files: Voron and Prusa printer parts, NASA models, 3DBenchy and a few well-known scanned
models. Think of them as a 100 MB folder of STL files. On "Smallest", STL-Harrington shrank that folder to about 6 MB, and on
"Fast" to about 6.5 MB. The best other tools that keep the shape exact got the same folder down to about 8 MB (meshoptimizer),
10 MB (OpenCTM) and 12 MB (Google's Draco). Zipping the STL files the usual way only got it down to about 28 MB. On every one
of the 27 test files, STL-Harrington on Smallest made the smallest file of all the exact tools.

WHAT COMES BACK
Every triangle, with every corner point bit-for-bit the same and the same facing (winding). The triangles may come back in a
different order and with their corners started at a different point, and the surface direction values (normals) are worked
out again from the corners; slicers use the corners, so the print is the same. The 80-byte label at the top of a binary STL is
kept. ASCII STL and OBJ files come back as binary STL. Every file is unpacked and checked before it is saved.

HOW TO USE IT (Windows 10 or 11, nothing to install)
1. Run STL-Harrington.exe.
2. Press "Add files..." or drag files or folders onto the window. STL and OBJ files are compressed; .stlh files are restored.
   Both kinds can be in the list at once.
3. Choose where to save: same folder, a USB stick or SD card (found automatically), or a folder you pick.
4. Choose the packing: Smallest (ZPAQ, slower to pack and unpack) or Fast (zstd, a little larger, unpacks fast).
5. Press Start. Originals are never changed; a name that is already taken gets " (2)" added.

GOOD TO KNOW
A .stlh file needs STL-Harrington to open it; slicers do not read .stlh directly. For files a slicer should open as they are,
use Harrington-3MF in the next folder instead. Windows may warn about an unknown publisher (the program is not code-signed).

FILES IN THIS FOLDER
STL-Harrington.exe      the program
METHODS.txt             how the coder works, step by step
STLH-FILE-FORMAT.txt    the .stlh file layout, byte by byte
RESULTS.txt             the full benchmark against meshoptimizer, OpenCTM, Draco and plain packers (27 files)
source/                 full source code and build notes (BUILD.txt)

Feedback: michaelhh@gmail.com, subject "STL-Harrington feedback" (or press "Send feedback" in the program).
