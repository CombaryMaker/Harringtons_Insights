3D-PRINTING - HARRINGTON-3MF AND STL-HARRINGTON
Michael Hugh Harrington, 15 November 2026. MIT License (see LICENSE.txt).
Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.

Two free tools for the files used in 3D printing. Both are first research prototypes for Windows, and both keep every corner
point of every model exactly - nothing is rounded or simplified.

Harrington-3MF/   Turns STL or OBJ files into ordinary 3MF files, about 40 to 46 percent smaller than 3MF files written the
                  usual way. Any slicer, printer or website that reads 3MF reads them, with nothing new to install.
                  FOR-3MF-MAKERS.txt explains the method for software and printer makers who may want to write 3MF this way.

STL-Harrington/   Packs STL or OBJ files into a much smaller .stlh file and unpacks them back to STL. About 15 to 17 to 1 on
                  typical printer files, smaller than every other exact mesh coder we tested, on all 27 test files (Smallest setting). For storing,
                  sharing and archiving; a .stlh file has to be restored to STL before a slicer can open it.

Each folder holds the program (.exe), a README, the methods, the results, and the full source code with build notes.

Feedback: michaelhh@gmail.com (subject "Harrington-3MF feedback" or "STL-Harrington feedback").
