hs_sieve - Harrington's Sets + record-length delta filter around the stock Sieve CLI
MIT License, Copyright (c) 2026 Michael Hugh Harrington

Sieve ("Sieve of Potentia", github.com/Thor110/Potentia) is AGPL-3.0 by Thor110. It is NOT included here and no Sieve
source is used. hs_sieve runs the separately built "sieve" program as an external tool: "sieve locate FILE --installer
OUT.sieve" (Sieve's default, size-optimised packing) and "sieve install X.sieve --to DIR".

What it does
1. Harrington's Sets multi-core decode. The input is labelled in 64 KB windows by 7 byte statistics (shares of control
bytes, letters, digits, spaces, high bytes and zeros, plus order-0 entropy) and k-means groups them into up to 3 sets
(a set is only added if it removes at least 15% of the remaining scatter); a one-byte-per-window map restores the order.
Each set is cut into independent blocks, the blocks are packed by sieve in parallel, and on decode all blocks are
unpacked in parallel on all cores, each piece written straight into its place in the output file. --sets 0 turns the
grouping off (plain block split).
2. Record-length detector and delta pre-filter. Autocorrelation of byte equality at lags 2..512 finds a fixed record
length. If one is found, each block is split into its byte columns (transpose of its full records), each column
optionally byte-delta coded (chosen by order-0 entropy). With --filter auto (default) each block is packed both with
and without the filter and the smaller is kept, so the filter is only used where it makes the output smaller.

Build:  ./build.sh   (g++ C++17; also build Sieve's "sieve" target yourself from the Potentia repo)
Use:    hs_sieve c IN OUT [--blocks N] [--jobs N] [--filter off|on|auto] [--sets 0|3] [--sieve PATH]
        hs_sieve d IN OUT [--jobs N] [--sieve PATH]
        (or set SIEVE=/path/to/sieve). --blocks defaults to the number of cores.

Not used: none of Michael Hugh Harrington's patent-pending private methods are in this program.
