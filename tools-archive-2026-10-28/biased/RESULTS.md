# Harrington Exchange on biased / odd-width data — 28 Oct 2026 (sizes: 1,000,000 bits down to X; zstd -19 back end; size only, decode not timed)
| Data | zstd -19 | xz -9e | Best known transform | Harrington Exchange (swap/juggle) |
|---|---|---|---|---|
| Synthetic 12-bit ADC-like noise, packed (524,288 B) | 821,426 | 770,485 | 701,948 (unpack to 16-bit + byte shuffle) | **621,665** (4/12) — wins by 11% vs best known |
| NIST biased random bits, packed (125,000 B) | 177,376 | 174,176 | 185,416 (bitshuffle) | **162,984** (13/21) — wins |
| Synthetic 5-bit skewed symbols (524,289 B) | 765,939 | 759,836 | 659,284 (unpack to bytes) | 660,644 (1/5) — tie |
| Real EEG, BDF 24-bit (first 1,048,576 B) | 502,449 | 329,193 | **308,527** (byte shuffle, 3 planes) | 321,048 (16/24) — loses to byte shuffle |
| NIST biased random bytes (1,000,000 B) | 327,568 | 334,836 | — | 321,620 (8/16) — small win |
| NIST ring oscillator bits / normal bytes / true-random 4-bit | — | — | — | ~0–0.5% (near incompressible or already handled) |
Sources: github.com/usnistgov/SP800-90B_EntropyAssessment (bin/), github.com/mne-tools/mne-testing-data (BDF). Synthetic seed 2360932695.
