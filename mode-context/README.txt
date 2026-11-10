MODE CONTEXT - an open idea for text compression
Michael Hugh Harrington, 10 November 2026. Public domain (CC0 1.0), see LICENSE.txt. Credit is welcome, never required.

THE IDEA
Text switches between modes that are written differently: in a novel, dialogue, narration and inner thought; in a
Wikipedia article, prose, headings, lists, templates and fact boxes, category and language links, formulas and the
reference section. Tell the compressor's predictor which mode it is in, and give that mode its own context. The text
usually announces its own mode (a quote mark, "==", "*", "{{", "[[Category:"), so the mode can be read from bytes
already decoded and costs nothing to store.

Everyone is invited to try their own versions of it, in any compressor. No permission or credit needed.

WHAT WAS MEASURED (details and every number in results/)
  Wikipedia, three 4 MiB slices of enwik8, inside lpaq1: a segment-type context (kind of the current line, kind of
  the previous line, inside a reference section or not; joined to order 2) made each slice 0.40% to 0.49% smaller
  than the same model with a plain context of the same size in that slot. Nothing is stored; every slice decoded
  exactly.
      slice   lpaq1 + plain context   lpaq1 + segment context
      s0           912,027                  907,665     (-0.48%)
      s40          925,201                  920,694     (-0.49%)
      s80          932,811                  929,105     (-0.40%)
  Novels (11 Project Gutenberg books): grouping dialogue paragraphs together, with a 1-bit-per-paragraph note to undo
  it (cost counted), and a free paragraph context (paragraph type, last two types, place in the paragraph) gave
  0.1% to 0.7% on dialogue-rich fiction with context-mixing compressors (kanzi -l 9, lpaq1, HCM, a small test model).
  A control reshuffle (odd paragraphs, then even) made every book larger with every tool, so the gain is from the
  grouping, not from reordering as such.

  Where it worked: context-mixing compressors (lpaq1, kanzi -l 9, HCM). Mixed: LZ-type (xz) and PPM (PPMd).
  Did not work: block-sorting compressors (BSC, bzip2) - their sort already gathers similar text, so a reordering
  note is pure cost. No effect on plain nonfiction prose.
  Did not work as tried: sentence position and quote state as extra contexts (the punctuation is already in the
  recent bytes), labels stored per paragraph from k-means, switching the mixer weights by mode, splitting dialogue
  by speaker turn, grouping Wikipedia lines (the note cost about as much as it saved, and nothing on top of the
  segment context).

  Not yet tried: richer mode contexts (mode with the current word, place inside a template line, template field
  names, reference-section layout), inner thought vs speech, scene mood, and any strong compressor such as paq8px
  or cmix. Gains usually shrink inside the strongest models; that is the open question.

HOW TO REPLICATE (Linux, g++, python3, curl, xz)
  1. bash scripts/get-data.sh      downloads the 11 books and enwik8, cuts the three Wikipedia slices into data/
  2. bash scripts/build.sh         builds the test programs into bin/
  3. bash scripts/run-wikipedia.sh  segment-type context (control vs segment build, decode checked) and line grouping
  4. bash scripts/run-books.sh      paragraph grouping and paragraph contexts on the books
     (set LPAQ1, KANZI, BSC to the stock programs to include them; grouped sizes need the printed note cost added)
  Sizes from bin/para and bin/ctxcm are the model's code length (sum of -log2 p, what an arithmetic coder reaches to
  within a few bytes). The lpaq1 builds are complete compressors and decompressors.

FILES
  src/lpaq1-wiki.cpp            lpaq1 + segment-type context for Wikipedia (-DCTRL = plain sparse context instead)
  src/lpaq1-para.cpp            lpaq1 + paragraph context (-DCTRL = word-pair context instead)
  src/lpaq1-para-wordpair.cpp   lpaq1 + word pair + paragraph context (-DCTRL = word pair + plain sparse context)
  src/para.cpp                  small test model with the paragraph variants (A, B, C, D, +m; see its header)
  src/ctxcm.cpp                 small test model for the sentence-position and quote signals (first test)
  src/reorder.py, reorder2.py   paragraph grouping schemes for the books (g2, g3, g4, control gc)
  src/wgroup.py                 line grouping by kind for Wikipedia (.wg) and its control (.wc)
  results/1..5                  the five experiments in order, with tables
  results/prior-art-check.txt   what is already known (XMill containers, document reordering, paq8px text model)
  results/raw-*.txt             raw sizes

LICENSES
  Everything here is CC0 1.0 (public domain) except the three modified lpaq1 files: lpaq1 is (C) 2007 Matt Mahoney
  under the GNU GPL, so those files stay under the GNU GPL.
