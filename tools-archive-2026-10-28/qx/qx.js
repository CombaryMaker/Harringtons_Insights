
/**********************************************************************
  Quad Exchange (Node.js) — FORWARD ONLY
  Purpose:
    - Read input file as bytes
    - Count nibble (4-bit) frequencies 0..15
    - Determine Swap1..Swap10 (10 nibbles stored in header)
    - Build a FULL 16→16 nibble permutation map:
        * Swap1..Swap5 originals map to TOP targets:
            [1111,1110,1101,1011,0111] => [15,14,13,11,7]
        * Swap6..Swap10 originals map to BOTTOM targets:
            [0001,0010,0100,1000,0000] => [1,2,4,8,0]
        * Remaining 6 originals map to remaining 6 targets
          by decimal-order alignment (lowest->lowest ... highest->highest)
    - Convert the file immediately (nibble-by-nibble) using that map
    - Output:
        - quad_converted.bin (binary)
        - QuadExchange_header_working.json (Swap1..Swap10 + stats)
        - Inspection_QuadExchange.txt (human audit)

  NO FrequencyCheck1 in this file.
  NO HaBiTS in this file.
  NO loop logic in this file.
**********************************************************************/

const fs = require("fs");

/**********************************************************************
  MODULE: Inspection
**********************************************************************/
function Inspection(moduleName, lines) {
  const fname = `Inspection_${moduleName}.txt`;
  fs.writeFileSync(fname, lines.join("\n") + "\n", "utf8");
}

/**********************************************************************
  Nibble helpers
**********************************************************************/
function hiNib(b) { return (b >> 4) & 0xF; }
function loNib(b) { return b & 0xF; }
function packByte(h, l) { return ((h & 0xF) << 4) | (l & 0xF); }
function nib4(n) { return n.toString(2).padStart(4, "0"); }

/**********************************************************************
  Fixed target slot order (your “standard desirability order”)

  Slots 1..5 targets (TOP):
    1) 1111 (15)
    2) 1110 (14)
    3) 1101 (13)
    4) 1011 (11)
    5) 0111 (7)

  Slots 6..10 targets (BOTTOM):
    6) 0001 (1)
    7) 0010 (2)
    8) 0100 (4)
    9) 1000 (8)
   10) 0000 (0)
**********************************************************************/
const TOP5_TARGETS = [15, 14, 13, 11, 7];
const BOT5_TARGETS = [1, 2, 4, 8, 0];

/**********************************************************************
  Deterministic sorting rules
  - “Most common”: count desc, then nibble value asc
  - “Least common”: count asc, then nibble value asc
**********************************************************************/
function sortMostCommon(counts) {
  const arr = [];
  for (let n = 0; n < 16; n++) arr.push({ n, c: counts[n] });
  arr.sort((a, b) => (b.c - a.c) || (a.n - b.n));
  return arr;
}

function sortLeastCommon(counts) {
  const arr = [];
  for (let n = 0; n < 16; n++) arr.push({ n, c: counts[n] });
  arr.sort((a, b) => (a.c - b.c) || (a.n - b.n));
  return arr;
}

/**********************************************************************
  CORE: Build mapping + convert bytes
**********************************************************************/
function QuadExchangeConvert(inputBytes) {
  // 1) Count nibble frequencies
  const counts = new Array(16).fill(0);
  for (let i = 0; i < inputBytes.length; i++) {
    counts[hiNib(inputBytes[i])] += 1;
    counts[loNib(inputBytes[i])] += 1;
  }

  // 2) Determine Top5 originals
  const most = sortMostCommon(counts);
  const top5 = most.slice(0, 5).map(x => x.n); // originals, in rank order

  // 3) Determine Bottom5 originals from remaining (so no overlap)
  const topSet = new Set(top5);
  const remainingAfterTop = [];
  for (let n = 0; n < 16; n++) if (!topSet.has(n)) remainingAfterTop.push(n);

  // Build a “least common” list over remainingAfterTop only
  const leastCandidates = remainingAfterTop.map(n => ({ n, c: counts[n] }));
  leastCandidates.sort((a, b) => (a.c - b.c) || (a.n - b.n));
  const bottom5 = leastCandidates.slice(0, 5).map(x => x.n); // originals, least->... order

  // 4) Build Swap1..Swap10 as stored ORIGINALS in your slot order:
  // Swap1..Swap5 = top5[0..4]
  // Swap6..Swap10 = bottom5 but reverse-loaded so that:
  //   Swap10 is absolute least common original
  // bottom5 is [least, 2nd least, ...], so:
  //   Swap10=bottom5[0], Swap9=bottom5[1], ..., Swap6=bottom5[4]
  const swaps1to5 = top5.slice();
  const swaps6to10 = [bottom5[4], bottom5[3], bottom5[2], bottom5[1], bottom5[0]];
  const swaps = swaps1to5.concat(swaps6to10); // length 10

  // 5) Remaining 6 originals
  const usedOriginals = new Set(swaps);
  const remaining6Originals = [];
  for (let n = 0; n < 16; n++) if (!usedOriginals.has(n)) remaining6Originals.push(n);
  remaining6Originals.sort((a, b) => a - b);

  // 6) Remaining 6 targets = outputs not in TOP5_TARGETS or BOT5_TARGETS, sorted by decimal
  const usedTargets = new Set([...TOP5_TARGETS, ...BOT5_TARGETS]);
  const remaining6Targets = [];
  for (let n = 0; n < 16; n++) if (!usedTargets.has(n)) remaining6Targets.push(n);
  remaining6Targets.sort((a, b) => a - b);

  if (remaining6Originals.length !== 6 || remaining6Targets.length !== 6) {
    throw new Error("QuadExchange: expected exactly 6 remaining originals and 6 remaining targets.");
  }

  // 7) Build full map[original] = target
  const map = new Array(16).fill(null);

  // Slot 1..5 originals -> TOP targets by slot
  for (let i = 0; i < 5; i++) map[swaps[i]] = TOP5_TARGETS[i];

  // Slot 6..10 originals -> BOTTOM targets by slot
  for (let i = 0; i < 5; i++) map[swaps[5 + i]] = BOT5_TARGETS[i];

  // Remaining 6 originals -> remaining 6 targets by decimal alignment
  for (let i = 0; i < 6; i++) map[remaining6Originals[i]] = remaining6Targets[i];

  // 8) Sanity: map must be a 16→16 permutation
  const usedOut = new Array(16).fill(0);
  for (let n = 0; n < 16; n++) {
    if (map[n] === null) throw new Error(`QuadExchange: missing mapping for nibble ${n}`);
    usedOut[map[n]]++;
  }
  for (let n = 0; n < 16; n++) {
    if (usedOut[n] !== 1) throw new Error(`QuadExchange: output nibble ${n} used ${usedOut[n]} times`);
  }

  // 9) Convert bytes immediately
  const outBytes = new Uint8Array(inputBytes.length);
  for (let i = 0; i < inputBytes.length; i++) {
    const h = map[hiNib(inputBytes[i])];
    const l = map[loNib(inputBytes[i])];
    outBytes[i] = packByte(h, l);
  }

  // 10) Build the “header working object” (Swap1..Swap10 + stats)
  const headerObj = {
    quadExchange: {
      swap1_to_swap10_dec: swaps,
      swap1_to_swap10_bin: swaps.map(n => nib4(n)),
      slotTargets_bin: [
        "1111","1110","1101","1011","0111",
        "0001","0010","0100","1000","0000"
      ]
    },
    stats: {
      inputBytes: inputBytes.length,
      totalNibbles: inputBytes.length * 2,
      counts_dec_0_to_15: counts,
      top5_originals_dec: top5,
      bottom5_originals_dec_least_to_more: bottom5,
      remaining6_originals_dec: remaining6Originals,
      remaining6_targets_dec: remaining6Targets,
      map_old_to_new_dec_0_to_15: map
    }
  };

  // 11) Inspection file (audit)
  const lines = [];
  lines.push("Module: Quad Exchange (FORWARD)");
  lines.push(`Input bytes: ${inputBytes.length}`);
  lines.push(`Total nibbles: ${inputBytes.length * 2}`);
  lines.push("");

  lines.push("Nibble counts (0..15):");
  for (let n = 0; n < 16; n++) {
    lines.push(`${n.toString().padStart(2, " ")} (${nib4(n)}): ${counts[n]}`);
  }
  lines.push("");

  lines.push("Swap1..Swap10 (ORIGINAL values stored in header slots):");
  for (let i = 0; i < 10; i++) {
    const target = (i < 5) ? TOP5_TARGETS[i] : BOT5_TARGETS[i - 5];
    lines.push(`Swap${i + 1}: ${swaps[i]} (${nib4(swaps[i])}) -> Target ${nib4(target)} (${target})`);
  }
  lines.push("");

  lines.push("Remaining6 originals (sorted): " + remaining6Originals.map(n => `${n}(${nib4(n)})`).join(" "));
  lines.push("Remaining6 targets  (sorted): " + remaining6Targets.map(n => `${n}(${nib4(n)})`).join(" "));
  lines.push("");

  lines.push("Full mapping old->new (0..15):");
  for (let n = 0; n < 16; n++) {
    lines.push(`${nib4(n)} (${n}) -> ${nib4(map[n])} (${map[n]})`);
  }

  Inspection("QuadExchange", lines);

  return { outBytes, headerObj };
}

/**********************************************************************
  RUNNER
**********************************************************************/
function runQuadExchangeOnly(inputFile, outBinFile = "quad_converted.bin") {
  const data = fs.readFileSync(inputFile);
  const bytes = new Uint8Array(data);

  const { outBytes, headerObj } = QuadExchangeConvert(bytes);

  fs.writeFileSync(outBinFile, Buffer.from(outBytes));
  fs.writeFileSync("QuadExchange_header_working.json", JSON.stringify(headerObj, null, 2), "utf8");

  return headerObj;
}

// Example:
// runQuadExchangeOnly("alice29.txt");

module.exports = {
  Inspection,
  QuadExchangeConvert,
  runQuadExchangeOnly
};