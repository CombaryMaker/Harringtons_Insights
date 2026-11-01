# hs_shark — tshark on every core, identical output

`hs_shark` runs several copies of Wireshark's `tshark` on slices of **one** capture file and merges the results so the
output is line-for-line identical to a single `tshark` run. Packets are sliced by conversation (host pair), not by
position, so streams, reassembly and request/response matching stay intact. See `ABOUT.txt` for the method.

**Desktop result** (6c/12t, tshark 4.6.9, 903 MB public capture, 964,896 packets, 3 runs each): plain tshark 16.1 s,
hs_shark 5.6 s (**2.9×**), output identical.

## Files

| file | what |
|---|---|
| `hs_shark.cpp` | source, version 1.2 (C++17, no dependencies beyond the OS; builds with g++/clang/MSVC or `zig c++`) |
| `hs_shark.exe` | Windows x64 build of 1.2 (static, no DLLs needed) |
| `bigcap.cpp` / `bigcap.exe` | builds the 903 MB public test capture from the two public pcap collections (no Python needed) |
| `shark_test_windows.ps1` | the desktop benchmark: plain vs classic vs direct, 3 runs, output compared |
| `results-desktop-2026-11-01.txt` | the desktop numbers and the bug the test found |
| `NOTES-1.0-design-and-cloud-results.txt`, `NOTES-1.1-direct-mode.txt` | design notes and the 2-core cloud results |

## Use

```
hs_shark CAPTURE.pcap --jobs 12 --out out.txt --mode direct -- -T fields -e frame.number -e ip.src -e ip.dst -e _ws.col.Info
hs_shark CAPTURE.pcap --jobs 12 --out out.txt --mode direct --check -- -T fields -e frame.number -e _ws.col.Info
```

Everything after `--` is passed to tshark unchanged. `--check` also runs plain tshark and compares every line.
`--tshark PATH` and `--editcap PATH` point at the Wireshark install if it is not on PATH (`C:\Program Files\Wireshark`
is found automatically on Windows). `--mode classic` writes shard files to disk instead of piping; it is slower but
useful for debugging.

## Build

```
g++ -O2 -std=c++17 -pthread hs_shark.cpp -o hs_shark            # Linux / macOS
zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hs_shark.cpp -o hs_shark.exe   # Windows cross-build
```

## Rebuilding the test capture

`bigcap DIR base.pcap big8.pcap` joins every Ethernet pcap under DIR and repeats it 8× with shifted times and altered
host addresses. The two public collections are github.com/chrissanders/packets and github.com/automayt/ICS-pcap
(their archive SHA-256 values are in the results file); convert pcapng files with Wireshark's `editcap -F pcap` first.

## Limits

Cross-host-pair analyses (a DNS answer linked to a later connection) are not combined across shards; filter by
address, port or protocol rather than frame number. Output identity has been verified for the fields and filters listed
in the notes; other dissector text that embeds frame numbers may need adding to the Info rewrite list in `hs_shark.cpp`
(that is how the tshark 4.6 "[TCP PDU reassembled in N]" case was fixed in 1.2).

## License

MIT License, Copyright (c) 2026 Michael Hugh Harrington. Wireshark/tshark are GPL-2 and are not included or modified.
Credits: Wireshark developers for tshark and editcap; Andrew Goodwin's review practices (controls, repeated verified runs)
shaped the test script.
