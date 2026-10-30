# Originality Statement — Combary and the Harrington Exchange
*Technical note for expert review. Michael Harrington, 28 October 2026. Measurements reproduced with source code and data references available on request.*

---

## 0. Scope and claim discipline

This note isolates the two contributions in the Harrington corpus whose **organising principle** we believe is not already the substance of a published method:

1. **Combary.** A ternary representation in which one symbol carries no value and conveys information only through its *position*. From it we derive three compression mechanisms.
2. **The Harrington Exchange.** A frequency-ranked, prefix-class-targeted permutation of fixed-width *J*-bit symbols, followed by a field split at bit *S*, with both parameters free over arbitrary non-byte widths.

For each, we state the function precisely, give the methodology, enumerate the closest prior art, and identify the differentiation. We also report where it fails. Results are compressed byte counts unless marked. Every reported round trip has been verified by decoding the actual serialised container.

---

## 1. Combary

### 1.1 Formal object

Let Σ₃ = {0, 1, 2}. A Combary string *z* ∈ Σ₃ⁿ is decomposed bijectively as

  *z* ↔ (*C*, *b*),  where *C* = { *i* : *z*ᵢ = 2 } ⊆ [*n*] with |*C*| = *k*, and *b* ∈ {0,1}ⁿ⁻ᵏ,

*b* being the subsequence of *z* at the positions not in *C*.

The symbol 2 is **value-free**: it contributes no payload bit, only membership in *C*. The representation therefore carries two information channels in one stream:

- a binary payload of *n − k* bits;
- a combinatorial side channel of log₂ C(*n*, *k*) bits, carried by the marker set.

The side channel is an index into the k-subsets of [*n*]. That is exactly what enumerative coding (Cover, 1973) and the combinatorial number system compute. Constant-weight codes are the coding-theory counterpart.

The original purpose was not compression. It was to let a single ternary medium carry two data types: storage, transmission, encryption. Four of the author's patents cover those uses. The compression work asks a narrower question:

> **When is coding a value-free positional marker cheaper than the information it isolates?**

Formally, a marker role is profitable iff

  *E*[ cost(*C* | model) ] < *E*[ bits saved in coding *b* given *C* ].

Every Combary-derived mechanism below is an instance of this inequality, with a different semantic assigned to "position *i* ∈ *C*".

### 1.2 Role A — "points elsewhere": the ternary pointer inside a context-mixing coder

**Function.** Before coding byte *x*ₜ, HCM forms a candidate *ĉ*ₜ:

- with an active long match: the byte the match predicts, at any match length;
- without a match: the last byte seen after the current order-4 context hash, carried with a repeat counter.

A binary event *e*ₜ = [*x*ₜ = *ĉ*ₜ] — the Combary "2" — is coded **first**.

- On *e*ₜ = 1, nothing further is coded for the byte.
- On *e*ₜ = 0, the byte is coded through the normal binary decomposition (two nibble trees) with ***ĉ*ₜ excluded**: the probability mass the model would have given to *ĉ*ₜ is removed and the rest renormalised along the path.

**Methodology** (key design decisions, each ablated on the 2,810,784-byte Canterbury corpus total):

- **P(*e*ₜ) is model-informed, not a fixed escape statistic.**
  - It is a logistic mix of 3–4 inputs: the model's own bitwise probability of *ĉ*ₜ (the product along its nibble path), a match-length / repeat-count state, and an adaptive flag history.
  - With a fixed escape table instead: 418,135 B against 414,602 B for the v3 baseline (worse).
  - Model-informed flag, 3-input mixer: 410,096 B. With the 4th input and the context candidate when no match is active: 408,427 B.
- **Exclusion is essential.**
  - The same pointer without exclusion: 449,073 B, 8.3% worse than having no pointer at all.
  - The flag costs bits on every miss, and exclusion is what turns a miss into information.
- **The underlying model learns the identical bit sequence with or without the pointer.**
  - The context statistics update on the true byte's bits regardless of *e*ₜ, so the pointer cannot degrade the predictor. It only changes what is transmitted.
  - This is why a mis-specified pointer costs only flag entropy, never model drift.
- **Integration result (rebuilt *hcmx*, published v3.3 code):**
  - every Canterbury file is equal or smaller;
  - fast mode 353,494 → 351,302 B; max mode 350,434 → 348,244 B;
  - best-of with record contexts 347,696 B;
  - Silesia (211,938,580 B), fast mode: 42,930,727 → 42,532,237 B.
  - Decode cost: +11–13%.
- **Back-end independence.** The pointer was transplanted into a BWT pipeline (bzip2-style: RLE → BWT → MTF), with MTF rank 0 as *ĉ* and the Huffman stage replaced by a binary arithmetic coder.
  - Control: the same pipeline with one extra context, 509,516 B.
  - With the pointer: 503,338 B (−1.2% beyond the control).
  - Real bzip2 -9: 542,709 B.

**Closest prior art and differentiation:**

| Prior art | What it does | Difference |
|---|---|---|
| **PPM escape + exclusion** (Cleary & Witten 1984; Moffat's PPMC) | Exclusion over the symbols of a failed context order; escape probability estimated from context counts | The Exchange of information is a single *pointed-to* symbol from an arbitrary source (match model or follower). The flag probability comes from the CM model's own prediction of that symbol, not from escape counts. Exclusion is done inside a binary decomposition without altering what the model learns. |
| **LZP** (Bloom 1996) | Hashes the context, predicts one byte, codes a match flag/length, optionally excludes on a miss | Structurally the closest ancestor. LZP is a stand-alone coder whose flag statistic comes from flag history. Here the flag is mixed from the full CM distribution's prediction of *ĉ* and acts as a pre-decision layer on top of an unchanged context-mixing model. LZP's flag replaces modelling; ours is conditioned on it. |
| **paq8/lpaq match model, SSE/APM** | Feeds the predicted next bit into the mixer as an input | The match model shapes probabilities bit by bit and never sends an explicit symbol-level event. The pointer sends one explicit event per byte and, on a miss, removes a symbol from the alphabet. That is a different factorisation of the same distribution, and the measured gain is on top of a CM that already has a match model. |
| **MTF rank-0 / symbol-ranking coders** (Fenwick) | The rank-0 hit is implicit in rank coding | In the BWT transplant, the explicit rank-0 event with a model-informed probability plus exclusion beat an equally-contexted control. |

**Claim:** a modest, verified, back-end-independent gain from coding a value-free positional decision with a model-derived probability, ahead of an unchanged model, with exclusion. The individual pieces exist; this arrangement, and its derivation from a positional-marker principle, is the author's.

### 1.3 Role B — "position is active": the Combary activity map

**Function.** Given record length *R* (detected automatically, §1.5), the input *x* is mapped to two streams:

- flags *F*ᵢ = [*x*ᵢ ≠ *x*ᵢ₋R], packed 1 bit per byte;
- literals *L* = (*x*ᵢ : *F*ᵢ = 1), stored raw, not as differences.

Each stream is compressed separately by a stock back end. Inverse: *x*ᵢ = *F*ᵢ ? *L*[*j*++] : *x*ᵢ₋R. Header ≈ 6 bytes.

**Results** (kennedy.xls, 1,029,744 B, *R* = 13):

| Back end | Plain | Record delta | Activity map | Decode |
|---|---|---|---|---|
| gzip -9 | 209,721 | 42,971 | **35,558** | 2,148 → ≈1,400 µs, faster than plain |
| LZ4 -12 | 324,136 | 43,653 | **39,997** | slower |
| Snappy | 422,640 | 129,763 | **69,635** | slower |
| LZFSE | 136,282 | 54,844 | **41,227** | slower |

**Ablations** (flag-stream bytes with gzip / LZ4 / zstd):

| Flag layout | Bytes |
|---|---|
| **1 bit per byte, packed (adopted)** | **12,324 / 13,533 / 9,640** |
| Per-record 13-bit masks in 2 bytes | 14,088 / 15,797 / 10,660 |
| Frequency-ranked mask codebook (361 distinct of 8,192) | 12,832 / 14,306 / 10,678 |

Storing the literals as deltas instead of raw bytes was larger on every file.

**Prior art and differentiation:**

- **JBIG TPGDON** (row-level "typical prediction") and **database change-mask / dirty-bitmap page encodings.** These mark unchanged rows or pages. The activity map works at byte granularity, with a record stride inferred from the data rather than from a known format geometry, and as a back-end-agnostic pre-pass.
- **Sparse delta / XOR-mask encodings.** These code the differences. The activity map keeps the literals in raw form and isolates *where*, not *how much*. The raw-literal choice was measured better because changed fields in tabular data are new values, not small increments.
- **Its place in the Combary family.** The flag stream *is* the marker set *C* with the semantic "position is active". The method succeeds where the profitability inequality holds: flag runs are long, so *H*(*C*) is small, while the literals carry nearly all the information.

**Limitation.** The per-byte reconstruction branch makes it decode-slower than fast LZ back ends. It is No-Pain only against gzip.

### 1.4 Role C — "cut here": the segment marker (negative result, reported)

Markers placed at block boundaries to switch transforms per block, at 4/16/64 KB, with zstd, LZ4 and gzip, lost in every configuration. On Canterbury and Silesia the boundaries rarely coincide with changes in the data's statistics, so *H*(*C*) exceeded the gain.

This is the inequality of §1.1 failing, and it is retained because the theory predicts it.

### 1.5 Supporting component (not claimed as novel)

Record length is detected by a byte-spacing histogram: the modal distance in [2, 512] since the previous occurrence of each byte value, over the first 200 KB. This is in the lineage of paq8's record model and RAR's automatic delta detection.

The contribution here is only to use it to parameterise stock coders and the Combary pre-passes under a strict decode-time acceptance test.

---

## 2. The Harrington Exchange

### 2.1 Definition

Fix a juggle width *J* ∈ [2, 24] and a swap width *S* ∈ [1, *J* − 1]; both may be odd and are not tied to byte boundaries. Parse the input bit string into symbols *X*₁ … *X*ₘ ∈ {0,1}ᴶ, carrying any tail of fewer than *J* bits verbatim.

1. **Rank.** Let *p̂* be the empirical pmf of *X*, and *s*₁, …, *s*_K the K most frequent symbols (ties broken stably).
2. **Target class order.** Enumerate *J*-bit patterns in the order *τ*_S:
   - first by the Hamming weight of the leading *S*-bit field, ascending;
   - then by that field's combination order;
   - then by the Hamming weight of the trailing (*J* − *S*)-bit field, ascending;
   - then by its combination order.

   Let *t*₁, …, *t*_K be the first K patterns.
3. **Permutation.** Define *σ* by *s*ᵢ ↦ *t*ᵢ. Complete it to a bijection on {0,1}ᴶ by mapping each displaced target (*t*ⱼ not among the *s*) to a freed source (*s*ᵢ not among the *t*), both lists sorted ascending. Every other symbol maps to itself.
   - Support |supp *σ*| ≤ 2K, so the decoder never materialises a 2ᴶ table unless it chooses to.
   - The decoder regenerates *τ*_S from (*J*, *S*, K), so the header stores only the K source symbols: **K·J bits**.
4. **Split.** For *Y*ᵢ = *σ*(*X*ᵢ), emit:
   - stream A = the leading *S* bits of each *Y*ᵢ;
   - stream B = the trailing *J* − *S* bits (historically reversed; ablation below shows reversal is inert).

   Each stream is serialised either bit-tight or right-aligned to whole bytes, and coded independently by a stock back end (zstd here).

The construction is invertible, and the container round trip is verified for all modes, both layouts, odd widths and non-zero tails.

### 2.2 What it computes

*σ* concentrates probability mass onto low-weight prefixes, so stream A behaves like a **data-adaptive slot index** and stream B like **extra bits within the slot**.

This is the structure of Deflate length/distance codes, LZMA's posSlot + footer bits, and Elias-γ / Golomb-Rice bucketing, with one essential difference. In all of those, slots are defined by numeric magnitude. In the Exchange, slots are defined by **frequency rank** under an arbitrary-width parse, and both fields are then given to a general LZ + entropy back end, rather than coding the extra bits raw.

The cost of separate coding is the chain-rule gap:

  *H*(*X*) = *H*(*A*) + *H*(*B* | *A*), but separate coding pays ≈ *H*(*A*) + *H*(*B*).

So the transform wins when:

- frequency-ranked prefixing makes *H*(*A*) small;
- *B* is nearly independent of *A*, so *I*(*A*; *B*) ≈ 0;
- the byte-oriented back end could not model the unaligned joint symbol directly.

That is the regime of **memoryless or weakly-dependent biased sources packed at non-byte widths**. It also explains why the method fails on magnitude-structured data (§2.5).

### 2.3 Evidence — ablation with a real container (zstd -19, bytes)

| Data (size) | zstd | xz -9e | Best known transform | **Full Exchange** | Split only | Rename only | No reversal |
|---|---|---|---|---|---|---|---|
| Synthetic 12-bit Gaussian, packed (524,288) | 430,664 | 403,956 | 368,027 (unpack to 16-bit + byte shuffle) | **324,507** (S8/J12) | 368,065 | 399,859 | 325,558 |
| NIST SP 800-90B biased bits, packed (125,000) | 22,172 | 21,772 | 22,558 (unpack + shuffle); 23,177 (bitshuffle) | **19,722** (S13/J21) | 22,375 | 25,909 | 19,715 |
| NIST biased bytes (1,000,000) | 327,568 | 334,836 | 323,054 (byte shuffle) | **321,128** (S8/J16) | 321,353 | 322,942 | 322,883 |
| Synthetic 5-bit geometric, packed (524,289) | 401,571 | 398,376 | **345,655** (unpack to bytes) | 346,370 | 346,363 | 345,698 | 346,354 |
| Real x-ray pixels packed to 12 bits (525,000) | 452,101 | 421,472 | 360,945 (unpack + shuffle) | 358,593 (S6/J12) | **342,496** (S8) | 438,477 | — |
| Real 24-bit EEG, BDF (1,048,576) | 526,856 | 345,184 | **323,516** (byte shuffle) | 335,895 | 334,882 | 532,373 | 335,713 |

**Reading:**

- **On biased sources the frequency permutation does work that splitting cannot.**
  - 12-bit: split-only 368,065 → full 324,507 (−11.8%).
  - Biased bits: 22,375 → 19,722 (−11.9%). The full Exchange also beats xz -9e (21,772) and bitshuffle.
  - Rename-only is worse than plain on both, so the gain is **synergistic**: the permutation is only useful because the split then separates the induced slot field.
- **Reversal of stream B is inert** (≤ 0.05% either way) and can be dropped from the specification.
- **On the 5-bit source the transform reduces to realignment.** Unpacking to bytes ties it, so the permutation adds nothing at low alphabet size.

### 2.4 Parameter landscape

The full 276-point sweep at zstd -19 (S = 1…23, J = S+1…24):

- **Biased bits:** a broad optimum (13/21, 15/23, 16/24, 14/22, 12/20, all within 0.5%). There is no natural symbol width here; the optimum tracks a block length at which the prefix-class histogram becomes sharply skewed.
- **12-bit source:** best S/J = 8/12, 7/12, 4/12. *J* locks to the true symbol width.
- **Structured byte files** (text, database): no odd *J* enters the top 30. The parse destroys the byte-aligned repetition the LZ stage depends on.

### 2.5 Where and why it fails

On real image data with numeric locality (x-ray, MRI, EEG), renaming **hurts**:

- Real 12-bit x-ray: full 358,593 against split-only 342,496.

Frequency ranking destroys numeric adjacency: successive pixels of similar *value* map to unrelated codewords, so the LZ matches and smooth statistics that magnitude order preserves are lost.

The operative rule is therefore a selector, not a universal transform:

- **apply *σ* when the source is near-memoryless with skewed marginals;**
- **apply split-only (magnitude slots) when the source has numeric locality.**

### 2.6 Cost

- **Encode:** O(*m* + *U* log *U*), with *U* the number of distinct symbols; parameter search is the dominant cost.
- **Decode:** O(*m*) with a direct 2ᴶ table or an O(log K) sparse lookup.

Measured with the compiled C decoder (median of 15):

| Data | Exchange decode | zstd decode | xz decode |
|---|---|---|---|
| 12-bit source | 5.1 ms | 0.89 ms | 19.2 ms |
| Biased bits | 1.49 ms | 0.12 ms | 1.13 ms |

On the 12-bit source the Exchange is both smaller and 3.8× faster to decode than xz.

### 2.7 Closest prior art and differentiation

| Prior art | Relation | Differentiation |
|---|---|---|
| **Bitshuffle** (Masui et al., 2015); HDF5/Blosc byte shuffle | Transposes typed data by bit or byte plane before LZ | Fixed type width and fixed plane order. No permutation of the alphabet. Planes are not grouped into a frequency-defined slot field. Measured worse on both biased-source files. |
| **Symbol ranking / MTF** (Bentley et al.; Fenwick) | Renames symbols adaptively by recency | Ranks by global frequency into a **prefix-weight class order**, at arbitrary bit width, and is meaningful only together with the field split. |
| **Slot + extra-bits codes** (Deflate, LZMA posSlot, Rice/Golomb, Elias-γ) | Factorise symbols into class + offset | Their classes are numeric magnitude ranges. The Exchange's classes are frequency-rank classes over an arbitrary parse, and both fields are LZ-and-entropy coded. |
| **CCSDS 121.0 / 123.0 preprocessing** | Maps prediction residuals to non-negative integers for Rice coding | Its mapping is a fixed sign-folding of residuals. *σ* is data-derived from the empirical pmf. |
| **Canonical Huffman** | Implies a frequency-sorted alphabet | That ordering is internal to an entropy coder. The Exchange externalises it as a reversible pre-transform, chosen to make a downstream field split effective. |

**Claim.** The specific combination of a frequency-ranked permutation onto a prefix-weight target order, its completion to a header-cheap bijection, and a free (S, J) field split at arbitrary bit widths has no exact match in the literature we checked. Its measured contribution beyond splitting is 11–12% on biased sources. A formal literature and patent search remains outstanding. We claim the combination and its demonstrated regime, not the constituent operations.

---

## 3. What unifies the two

Both contributions treat ***where* information sits** as a separately codable quantity:

- Combary separates value from position.
- The Exchange separates frequency class from offset.

In each case, the measured gains appear precisely when the separated component is cheap to code relative to what it isolates, and disappear, sometimes reversing sign, when it is not. Both principles state that condition in advance, and the negative results are consistent with it.

*Raw data, source code (Python reference encoder/decoder, C decoder) and test-file provenance (NIST SP 800-90B sample data; MNE testing data; Canterbury and Silesia corpora; synthetic seed 2360932695) are available for independent reproduction.*
