HARRINGTON-3MF RESEARCH ENCODER - first prototype (0.1)
Michael Hugh Harrington, 15 November 2026. MIT License.
Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.

WHAT IT DOES
Turns STL or OBJ files into standard 3MF files that Cura, PrusaSlicer, Bambu Studio, OrcaSlicer and other 3MF readers open
as they are. The 3MF files come out about 40 to 46 percent smaller than 3MF files written the usual way, and about 85 percent
smaller than the STL files, with every coordinate kept exactly.

HOW TO USE IT (Windows 10 or 11, nothing to install)
1. Run Harrington-3MF.exe.
2. Press "Add files..." or drag STL / OBJ files or whole folders onto the window.
3. Under "Save the 3MF files to", keep "Same folder as each original file", pick a USB stick or SD card (they are found
   automatically, and one plugged in while the program is open is picked straight away), or choose a folder.
4. Press "Make 3MF files". Each file is checked after it is made; a file that fails the check is not saved.
Your original files are never changed. A name that is already taken gets " (2)" added.

GOOD TO KNOW
Large files take a while, because the program uses the strongest standard packing (zopfli). Triangles that repeat a corner
(flat, zero-area triangles) are left out, because the 3MF standard does not allow them; they do not print.
Windows may warn about an unknown publisher the first time, because the program is not code-signed.

FILES IN THIS FOLDER
Harrington-3MF.exe     the program
FOR-3MF-MAKERS.txt     the case for slicer, CAD, printer and website makers to write 3MF this way
METHODS.txt            exactly what the encoder does, step by step
RESULTS.txt            measured sizes and times on 20 real files
source/                full source code and build notes (BUILD.txt)

Feedback: michaelhh@gmail.com, subject "Harrington-3MF feedback" (or press "Send feedback" in the program).
