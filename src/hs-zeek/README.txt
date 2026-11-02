hs-zeek - Harrington's Sets for Zeek: one saved capture, many cores, output identical to plain Zeek
Michael Hugh Harrington, 2026. MIT License.

WHAT IT DOES
Zeek analyses a saved capture (pcap) on one core. hs-zeek splits one capture into conversation sets (every packet
goes to a set by host pair; DHCP is one set; non-IP traffic is one set), runs several Zeek processes at once, one per
shard, and merges their logs into the log set a single Zeek process writes - every row identical.

RESULT (desktop: AMD Ryzen 5 7500X3D, 6 cores / 12 threads, WSL 2 Ubuntu 24.04, Zeek 8.0.4)
  capture   plain Zeek   12 shards   speed    rows identical to plain Zeek
  903 MB      6.54 s       4.35 s    1.50x    35,920 of 35,920
  7.2 GB     50.34 s      36.32 s    1.39x    284,416 of 284,416
  All 17 compared logs (conn, dns, http, files, ftp, smtp, snmp, ssl, x509, dhcp, weird, notice, signatures,
  known_hosts, known_certs, known_services, software) identical at 2, 4, 6, 8 and 12 shards. Only connection IDs
  (random per process) are ignored, and process-health logs (stats, loaded_scripts, packet_filter, capture_loss,
  reporter, telemetry) are not compared.
  Conditions, in the plain run and the shard runs alike: max_timer_expires = 0 (notimerthrottle.zeek) and Zeek's
  online malware-hash lookups off (offline.zeek). With either left on, even two plain Zeek runs can differ.

HOW EXACTNESS IS KEPT
  1. Clock ticks: every shard also gets 60-byte EtherType 0x88B5 frames at the times of other shards' packets
     (only when its clock is 10 ms or more behind), so each Zeek's timers fire at the same network times as in the
     plain run. Zeek logs nothing for these frames.
  2. Raw inputs + replay: software.log and known_services.log depend on per-host tables that no single shard sees
     whole. Each shard records the raw inputs (hs_shard.zeek); the merge replays Zeek 8's own rules over all of them
     in network-time order. known_hosts / known_certs are kept once per 24 h, as Zeek's stores do.
  3. Other logs are joined shard by shard, as a Zeek cluster's logger does.

TEST DATA (public only)
  69 public captures from github.com/chrissanders/packets (pcapng converted by ng2pcap.py, put in time order by
  mono.py), joined and repeated: 8 copies = 903,342,560 bytes (SHA-256 b3db1ba8...), 64 copies 10,000,000 s apart =
  7,226,740,312 bytes (SHA-256 93a45045...). bigcap.cpp / bigcapn.cpp rebuild both exactly.

FILES
  hs_zeek_par.cpp      runner: plans sets and shards in memory, feeds each Zeek through a pipe, times each shard
  hs_zeek_par1.cpp     one-pass runner (reads the capture once; exact but slower - its single reader sets the pace)
  hs_pcapsplit.cpp     splitter that writes shard files (--ticks, --ticks=G)
  hs_shard.zeek        loaded in every shard run: records raw inputs for software and known_services
  hs_zeek_merge.py     merge (parallel, one log per core)
  zeekcmp.py           row-by-row comparison with plain Zeek
  notimerthrottle.zeek, offline.zeek   the two settings above
  bigcap.cpp, bigcapn.cpp, ng2pcap.py, mono.py   test capture builders
  run_pc.sh, run_par*.sh, ticks.sh, offline.sh, go.sh, start.ps1, bench_zeek*.sh   the exact test scripts
  ZEEK-EXACT-MATCH.txt full write-up, including the steps, the dead ends and a Zeek oddity at far-future timestamps
  log_zeek*.txt        raw output of every run
