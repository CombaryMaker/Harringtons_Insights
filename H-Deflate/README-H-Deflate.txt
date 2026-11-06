H-DEFLATE v1.01 (6 November 2026; v1 was 4 November 2026) - our own deflate unpacker / exact re-packer
Michael Hugh Harrington. Patent Pending (U.S. Provisional App. 64/192,843). Not MIT - see LICENSE.txt.

WHAT IT DOES
  hdeflate pack IN OUT [threads]    find every deflate stream in IN (PNG, ZIP entries, gzip members, zlib streams such as
                                    PDF FlateDecode and Office pictures, streams inside streams), replace each with its
                                    raw bytes plus a small reconstruction record, write the HDF1 container to OUT
  hdeflate unpack IN OUT [threads]  rebuild the original file byte for byte
  hdeflate test IN [threads] [csv]  pack, unpack, compare; one CSV line per stream (kind, bytes, record, settings)
  Then HX Complete compresses the container; HX 13 routes the unpacked pixel data to the image specialists.

HOW IT WORKS (short)
  1. A recording inflater keeps every token, block boundary and dynamic-header detail of the stream.
  2. Our own deflate matcher (lazy hash chains with the zlib family's decision rules, parametrised wider) re-predicts the
     token stream; settings are searched per stream (likely ones first, scored on a prefix, best few run in full).
     zlib-made streams rebuild from a record of 15-120 bytes with zero corrections (bit-exact with the system zlib on
     678 setting combinations, levels 0-9, every memory level, strategy and window size).
  3. Streams from other encoders (7-Zip, Office, Adobe, Apple) use the "rank" predictor: at each token the useful
     candidates are listed (nearest first, strictly longer as they get farther) and the actual choice is coded as
     as-predicted / literal / candidate rank / shorter length / k-th occurrence; the codes go to dense per-field
     streams that HX Complete models. A stream whose record would cost more than 25% of its size is left as it was.
  4. Block types and Huffman headers are predicted the zlib way; anything else is stored explicitly.
  5. PNG pixel rows (and PDF Flate images with PNG predictors) are unfiltered to plain pixels with a PNM header, the
     per-row filter types kept in the record; HX 13 finds those images and sends them to JPEG XL lossless (BSD), the
     rest goes through the normal HX Complete routes. Embedded JPEGs go to JPEG XL's lossless JPEG transcoding.
  6. Everything is verified at pack time; any stream that does not rebuild exactly stays verbatim.

LICENCE POSITION
  Written from RFC 1951 and the published description of the zlib method; no code from zlib, precomp, preflate or 7-Zip.
  Deflate is an open format with no live patents; the recompression idea has been public since 2006 (precomp).
  Surprise found 4 Nov 2026: the precomp 0.4.8 source and its preflate library are Apache 2.0 (LICENSE, README,
  source headers); only an old banner string says "free for non-commercial use". Noted for the attorney; H-Deflate is
  still the better path because it is ours to license and it fits the pipeline.

FILES
  src/hd_bits.h      bit reader/writer, varints
  src/hd_trees.h     deflate tables, Huffman builder with the zlib tie rules, block header coding
  src/hd_match.h     the matcher (predict / commit)
  src/hd_inflate.h   recording inflater
  src/hd_diff.h      record version 1 (zlib family): corrections, block prediction, pack/unpack of one stream
  src/hd_rank.h      record version 2 (rank predictor) for other encoders
  src/hd_search.h    per-stream settings search with learned priors
  src/hdeflate.cpp   scanner, recursive container, PNG/PDF image unfiltering, CLI
  src/analyze.cpp    development tool: how a stream differs from our prediction
  inventory.py       first survey (Python/zlib) of the streams in the test sets
  hd_report.py       results report generator
  hdeflate.exe       Windows build (zig cross-compile); hdeflate = Linux build
  build: g++ -O2 -std=c++17 src/hdeflate.cpp -o hdeflate -lpthread
  HX 13 = bent/hxf.cpp (mode 11, embedded images) built like HX 12.

KNOWN LIMITS / NEXT
  - Interlaced PNGs and 16-bit PDF images are not unfiltered (left as filtered rows); palette PNGs get a PAM header
    HX does not route yet.
  - 7-Zip / Office / Adobe streams: records cost 5-15% of the stream; a price-aware predictor could halve that.
  - Pack time is search-bound (about 1-4 s per MB); unpack is one deflate pass per stream (zlib -9 speed).
  - Multi-threaded over streams; a single huge stream is one thread.
