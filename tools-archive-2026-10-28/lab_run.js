
// ===================== HCM ENGINE (pure functions) =====================
const LN2 = Math.LN2, LNPI = Math.log(Math.PI);
const LG = [0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313, -176.61502916214059,
  12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7];
function lgamma(x) {
  if (x < 0.5) return Math.log(Math.PI / Math.sin(Math.PI * x)) - lgamma(1 - x);
  x -= 1; let a = LG[0]; const t = x + 7.5;
  for (let i = 1; i < 9; i++) a += LG[i] / (x + i);
  return 0.5 * Math.log(2 * Math.PI) + (x + 0.5) * Math.log(t) - t + Math.log(a);
}
function kt(z, o) { // ideal adaptive cost in bits of a binary sequence with z zeros and o ones
  if (z + o === 0) return 0;
  return -(lgamma(z + 0.5) + lgamma(o + 0.5) - LNPI - lgamma(z + o + 1)) / LN2;
}
function bytesToBits(u8) {
  const b = new Uint8Array(u8.length * 8);
  for (let i = 0; i < u8.length; i++) { const v = u8[i]; for (let j = 0; j < 8; j++) b[i * 8 + j] = (v >> (7 - j)) & 1; }
  return b;
}
function complement(b) { const o = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) o[i] = b[i] ^ 1; return o; }
function eqBits(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
function hashBits(b) {
  let h1 = 2166136261 >>> 0, h2 = 5381 >>> 0;
  for (let i = 0; i < b.length; i++) { h1 ^= b[i]; h1 = Math.imul(h1, 16777619) >>> 0; h2 = (Math.imul(h2, 33) + b[i] + 1) >>> 0; }
  return h1 + ':' + h2 + ':' + b.length;
}
function prefixOnes(b) { const P = new Uint32Array(b.length + 1); for (let i = 0; i < b.length; i++) P[i + 1] = P[i] + b[i]; return P; }

// ---- coders ----
function costBalance(b) { // order-0 adaptive; best of whole stream or fixed segments (2-bit choice)
  const n = b.length, P = prefixOnes(b); let best = kt(n - P[n], P[n]);
  for (const seg of [16384, 4096, 1024]) {
    let c = 0;
    for (let s = 0; s < n; s += seg) { const e = Math.min(n, s + seg); const o = P[e] - P[s]; c += kt(e - s - o, o); }
    if (c < best) best = c;
  }
  return best + 2;
}
function costContext(b, k) { // each bit predicted from the previous k bits, adaptive
  const size = 1 << k, mask = size - 1; const c0 = new Uint32Array(size), c1 = new Uint32Array(size);
  let ctx = 0, cost = 0;
  for (let i = 0; i < b.length; i++) {
    const z = c0[ctx], o = c1[ctx], bit = b[i];
    cost -= Math.log2(((bit ? o : z) + 0.5) / (z + o + 1));
    if (bit) c1[ctx]++; else c0[ctx]++;
    ctx = ((ctx << 1) | bit) & mask;
  }
  return cost;
}
function pureBits(b) {
  const W = 256; let pure = 0;
  for (let s = 0; s + W <= b.length; s += W) { let o = 0; for (let i = s; i < s + W; i++) o += b[i]; if (o <= W * 0.05 || o >= W * 0.95) pure += W; }
  return pure;
}

// ---- shared swap-table helpers (your shrinking-index table cost) ----
function tableCost(N, k) { let c = 0; for (let i = 0; i < k; i++) if (N - i > 1) c += Math.ceil(Math.log2(N - i)); return c; }
function topK(freq, k) {
  const idx = Array.from(freq.keys()).sort((a, b) => (freq[b] - freq[a]) || (a - b));
  return idx.slice(0, k);
}
function buildPerm(N, F, B) {
  const pi = new Int32Array(N).fill(-1), used = new Uint8Array(N);
  for (let i = 0; i < F.length; i++) { pi[F[i]] = B[i]; used[B[i]] = 1; }
  const pending = [];
  for (let s = 0; s < N; s++) if (pi[s] === -1) { if (!used[s]) { pi[s] = s; used[s] = 2; } else pending.push(s); }
  const free = []; for (let c = 0; c < N; c++) if (!used[c]) free.push(c);
  for (let i = 0; i < pending.length; i++) pi[pending[i]] = free[i];
  return pi;
}
function invertPerm(pi) { const inv = new Int32Array(pi.length); for (let s = 0; s < pi.length; s++) inv[pi[s]] = s; return inv; }
function popc(v) { let c = 0; while (v) { v &= v - 1; c++; } return c; }
const _bestOrder = {};
function bestOrder(m, dir) { // your premade order: best codes first
  const key = m + ':' + dir; if (_bestOrder[key]) return _bestOrder[key];
  const all = Array.from({ length: 1 << m }, (_, i) => i); let out;
  if (dir === 1) {
    const pref = m === 4 ? [15, 14, 11, 13, 7] : [];
    out = pref.concat(all.filter(v => !pref.includes(v)).sort((a, b) => (popc(b) - popc(a)) || (b - a)));
  } else {
    const pref = m === 4 ? [0, 1, 4, 2, 8] : [];
    out = pref.concat(all.filter(v => !pref.includes(v)).sort((a, b) => (popc(a) - popc(b)) || (a - b)));
  }
  return (_bestOrder[key] = out);
}
const _terOrder = {};
function terOrder(m) { // trit codes with the most 2s first (a 2 costs 1 bit in HaBiTS, a 0 or 1 costs 2)
  if (_terOrder[m]) return _terOrder[m];
  const N = 3 ** m, twos = v => { let c = 0; for (let j = 0; j < m; j++) { if (v % 3 === 2) c++; v = Math.floor(v / 3); } return c; };
  return (_terOrder[m] = Array.from({ length: N }, (_, i) => i).sort((a, b) => (twos(b) - twos(a)) || (a - b)));
}

// ---- HaBiTS core: '0' -> trit 2, '10' -> trit 0, '11' -> trit 1 ----
const PERMS = [[0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0]];
const ID2T = [2, 0, 1], T2ID = [1, 2, 0], IDPERM = [0, 1, 2];
function habitsParse(b, cin) { // block ids: 0='0', 1='10', 2='11' (on b xor cin)
  const n = b.length, ids = new Uint8Array(n); let c = 0, i = 0, tail = 0;
  while (i < n) {
    if ((b[i] ^ cin) === 0) { ids[c++] = 0; i++; }
    else if (i + 1 < n) { ids[c++] = (b[i + 1] ^ cin) ? 2 : 1; i += 2; }
    else { tail = 1; i++; }
  }
  return { ids: ids.subarray(0, c), tail };
}
function habitsEncode(ids, perm, tail) {
  let len = tail; for (let i = 0; i < ids.length; i++) len += perm[ids[i]] === 0 ? 1 : 2;
  const o = new Uint8Array(len); let p = 0;
  for (let i = 0; i < ids.length; i++) { const v = perm[ids[i]]; if (v === 0) o[p++] = 0; else { o[p++] = 1; o[p++] = v === 2 ? 1 : 0; } }
  if (tail) o[p++] = 1;
  return o;
}

// ---- stage: Sort (binary swap) ----
function sortApply(b, m, k, dir) {
  const N = 1 << m, nb = Math.floor(b.length / m), vals = new Uint16Array(nb), freq = new Uint32Array(N);
  for (let i = 0; i < nb; i++) { let v = 0; for (let j = 0; j < m; j++) v = (v << 1) | b[i * m + j]; vals[i] = v; freq[v]++; }
  let present = 0; for (let s = 0; s < N; s++) if (freq[s]) present++;
  const kk = k >= N ? N : Math.min(k, present);
  const F = topK(freq, kk), pi = buildPerm(N, F, bestOrder(m, dir).slice(0, kk));
  const out = new Uint8Array(b.length);
  for (let i = 0; i < nb; i++) { const v = pi[vals[i]]; for (let j = 0; j < m; j++) out[i * m + j] = (v >> (m - 1 - j)) & 1; }
  for (let i = nb * m; i < b.length; i++) out[i] = b[i];
  return { out, hdr: tableCost(N, kk) + 8 + (m + 1), rec: { t: 'B', m, k: kk, full: kk === N, dir, F } };
}
function sortInvert(o, r) {
  const N = 1 << r.m, m = r.m, inv = invertPerm(buildPerm(N, r.F, bestOrder(m, r.dir).slice(0, r.k)));
  const nb = Math.floor(o.length / m), x = new Uint8Array(o.length);
  for (let i = 0; i < nb; i++) { let v = 0; for (let j = 0; j < m; j++) v = (v << 1) | o[i * m + j]; v = inv[v]; for (let j = 0; j < m; j++) x[i * m + j] = (v >> (m - 1 - j)) & 1; }
  for (let i = nb * m; i < o.length; i++) x[i] = o[i];
  return x;
}

// ---- stage: Ternary Sort (HaBiTS parse, swap m-trit blocks, back to bits) ----
function tsortApply(b, m, k) {
  const N = 3 ** m; let best = null;
  for (const cin of [0, 1]) {
    const P = habitsParse(b, cin), c = P.ids.length, nb = Math.floor(c / m);
    const vals = new Uint16Array(nb), freq = new Uint32Array(N);
    for (let i = 0; i < nb; i++) { let v = 0, mul = 1; for (let j = 0; j < m; j++) { v += ID2T[P.ids[i * m + j]] * mul; mul *= 3; } vals[i] = v; freq[v]++; }
    let present = 0; for (let s = 0; s < N; s++) if (freq[s]) present++;
    const kk = k >= N ? N : Math.min(k, present);
    const F = topK(freq, kk), pi = buildPerm(N, F, terOrder(m).slice(0, kk));
    const ids = new Uint8Array(c);
    for (let i = 0; i < nb; i++) { let v = pi[vals[i]]; for (let j = 0; j < m; j++) { ids[i * m + j] = T2ID[v % 3]; v = Math.floor(v / 3); } }
    for (let i = nb * m; i < c; i++) ids[i] = P.ids[i];
    const out = habitsEncode(ids, IDPERM, P.tail);
    if (!best || out.length < best.out.length) best = { out, cin, F, kk };
  }
  return { out: best.out, hdr: tableCost(N, best.kk) + 8 + (2 * m + 1) + 1, rec: { t: 'T', m, k: best.kk, full: best.kk === N, F: best.F, cin: best.cin } };
}
function tsortInvert(o, r) {
  const m = r.m, N = 3 ** m, inv = invertPerm(buildPerm(N, r.F, terOrder(m).slice(0, r.k)));
  const P = habitsParse(o, 0), c = P.ids.length, nb = Math.floor(c / m), ids = new Uint8Array(c);
  for (let i = 0; i < nb; i++) {
    let v = 0, mul = 1; for (let j = 0; j < m; j++) { v += ID2T[P.ids[i * m + j]] * mul; mul *= 3; }
    v = inv[v]; for (let j = 0; j < m; j++) { ids[i * m + j] = T2ID[v % 3]; v = Math.floor(v / 3); }
  }
  for (let i = nb * m; i < c; i++) ids[i] = P.ids[i];
  const x = habitsEncode(ids, IDPERM, P.tail);
  return r.cin ? complement(x) : x;
}

// ---- stage: HaBiTS remap pass ----
function habitsApply(b, cin, p) {
  const P = habitsParse(b, cin);
  return { out: habitsEncode(P.ids, PERMS[p], P.tail), hdr: 5, rec: { t: 'H', cin, p } };
}
function habitsInvert(o, r) {
  const P = habitsParse(o, 0), perm = PERMS[r.p], inv = [0, 0, 0];
  for (let i = 0; i < 3; i++) inv[perm[i]] = i;
  const x = habitsEncode(P.ids, inv, P.tail);
  return r.cin ? complement(x) : x;
}

// ---- stage: Juggle (trigger scan, odd lines to the front, even lines reversed to the back) ----
function windows8(b) { const n = b.length, w = new Uint8Array(n); let v = 0; for (let i = n - 1; i >= 0; i--) { v = ((v >> 1) | (b[i] << 7)) & 255; w[i] = v; } return w; }
function makeLut(vals) { const lut = new Uint8Array(256); for (const v of vals) lut[v] = 1; return lut; }
function juggleScore(w, P, n, L, lut) {
  const sh = 8 - L; let i = 0, start = 0, line = 0, fo = 0, fn = 0, bo = 0, bn = 0;
  while (i + L <= n) {
    if (lut[w[i] >> sh]) { i += L; const o = P[i] - P[start]; if (line & 1) { bo += o; bn += i - start; } else { fo += o; fn += i - start; } line++; start = i; }
    else i++;
  }
  const o = P[n] - P[start]; if (line & 1) { bo += o; bn += n - start; } else { fo += o; fn += n - start; }
  return kt(fn - fo, fo) + kt(bn - bo, bo);
}
function juggleApply(b, L, vals) {
  const n = b.length, w = windows8(b), lut = makeLut(vals), sh = 8 - L, out = new Uint8Array(n);
  let i = 0, start = 0, line = 0, f = 0, q = n - 1;
  const emit = (s, e) => { if (line & 1) { for (let j = s; j < e; j++) out[q--] = b[j]; } else { for (let j = s; j < e; j++) out[f++] = b[j]; } line++; };
  while (i + L <= n) { if (lut[w[i] >> sh]) { i += L; emit(start, i); start = i; } else i++; }
  if (start < n) emit(start, n);
  return { out, hdr: 4 + 1 + L * vals.length + 2, rec: { t: 'J', L, vals: vals.slice() } };
}
function juggleInvert(o, r) {
  const L = r.L, lut = makeLut(r.vals), n = o.length, x = new Uint8Array(n);
  let p = 0, lo = 0, hi = n, odd = true;
  while (lo < hi) {
    const rem = hi - lo; let len = rem;
    if (odd) {
      for (let k = 0; k + L <= rem; k++) { let v = 0; for (let j = 0; j < L; j++) v = (v << 1) | o[lo + k + j]; if (lut[v]) { len = k + L; break; } }
      for (let j = 0; j < len; j++) x[p++] = o[lo + j]; lo += len;
    } else {
      for (let k = 0; k + L <= rem; k++) { let v = 0; for (let j = 0; j < L; j++) v = (v << 1) | o[hi - 1 - (k + j)]; if (lut[v]) { len = k + L; break; } }
      for (let j = 0; j < len; j++) x[p++] = o[hi - 1 - j]; hi -= len;
    }
    odd = !odd;
  }
  return x;
}
function juggleCandidates(b, Lmin, Lmax, keep, pairs) {
  const n = b.length, w = windows8(b), P = prefixOnes(b), res = [];
  for (let L = Lmin; L <= Lmax; L++) for (let v = 0; v < (1 << L); v++) res.push({ sc: juggleScore(w, P, n, L, makeLut([v])), L, vals: [v] });
  res.sort((a, c) => a.sc - c.sc);
  const out = res.slice(0, keep);
  if (pairs && res.length) {
    const top = res[0]; let bp = null;
    for (let v = 0; v < (1 << top.L); v++) {
      if (v === top.vals[0]) continue;
      const sc = juggleScore(w, P, n, top.L, makeLut([top.vals[0], v]));
      if (!bp || sc < bp.sc) bp = { sc, L: top.L, vals: [top.vals[0], v] };
    }
    if (bp) out.push(bp);
  }
  return out.map(x => ({ t: 'J', L: x.L, vals: x.vals }));
}

// ---- dispatch ----
function applyVariant(b, v) {
  if (v.t === 'B') return sortApply(b, v.m, v.k, v.dir);
  if (v.t === 'T') return tsortApply(b, v.m, v.k);
  if (v.t === 'H') return habitsApply(b, v.cin, v.p);
  return juggleApply(b, v.L, v.vals);
}
function invertRec(o, r) {
  if (r.t === 'B') return sortInvert(o, r);
  if (r.t === 'T') return tsortInvert(o, r);
  if (r.t === 'H') return habitsInvert(o, r);
  return juggleInvert(o, r);
}
function bitsStr(v, L) { let s = ''; for (let j = L - 1; j >= 0; j--) s += (v >> j) & 1; return s; }
function labelOf(r) {
  if (r.t === 'B') return 'Sort' + r.m + ' ' + (r.full ? 'full' : 'top' + r.k) + (r.dir ? '→1' : '→0');
  if (r.t === 'T') return 'TSort' + r.m + ' ' + (r.full ? 'full' : 'top' + r.k);
  if (r.t === 'H') return 'HaBiTS ' + (r.cin ? 'flip ' : '') + 'map' + (r.p + 1);
  return 'Juggle ' + r.vals.map(v => bitsStr(v, r.L)).join('+');
}
function parsePattern(line) {
  let s = line.trim(); if (!s) return null;
  const rep = /\(([^()]*)\)\s*[x×*]\s*(\d+)/i;
  while (rep.test(s)) s = s.replace(rep, (_, inner, k) => Array(Math.max(1, +k)).fill(inner.trim()).join(' > '));
  const map = { sort: 'B', bsort: 'B', swap: 'B', tsort: 'T', ternary: 'T', ternarysort: 'T', tsw: 'T', habits: 'H', juggle: 'J', any: 'A' };
  const slots = s.split(/->|>|,/).map(x => x.trim().toLowerCase().replace(/\s+/g, '')).filter(Boolean);
  const out = [];
  for (const t of slots) { if (!(t in map)) throw new Error('Unknown step "' + t + '". Use Sort, TSort, HaBiTS, Juggle or Any.'); out.push(map[t]); }
  return out;
}

const fs=require('fs');
function makeExample(){ let seed=20261024; const rnd=()=> (seed=(Math.imul(seed,1664525)+1013904223)>>>0)/4294967296;
 const gates=['gate-1','gate-2','gate-3','pool','clubhouse','east-ped'], res=['granted','granted','granted','denied','held-open']; let s='',t=18*3600;
 while(s.length<140000){ t+=1+Math.floor(rnd()*90); const hh=String(Math.floor(t/3600)%24).padStart(2,'0'),mm=String(Math.floor(t/60)%60).padStart(2,'0'),ss=String(t%60).padStart(2,'0');
  const g=gates[Math.floor(rnd()*gates.length)], r=res[Math.floor(rnd()*res.length)]; s+=`2026-10-24 ${hh}:${mm}:${ss} INFO ${g} badge=${1000+Math.floor(rnd()*400)} result=${r}\n`; }
 return Buffer.from(s); }
const stat=[];
for(const m of [4,8]){ const N=1<<m; const ks=[4,8,16,32,64].filter(k=>k<N); ks.push(N); for(const k of ks) stat.push({t:'B',m,k,dir:1}); }
for(const m of [2,3,4,5]){ const N=3**m; const ks=[3,9,27].filter(k=>k<N); ks.push(N); for(const k of ks) stat.push({t:'T',m,k}); }
for(const cin of [0,1]) for(let p=0;p<6;p++) stat.push({t:'H',cin,p});
function run(label,data,coder){
 const orig=bytesToBits(data.subarray(0,16384)); const sc=b=> coder==='context'?costContext(b,12):costBalance(b);
 const base=sc(orig)+32; const board=[]; const visited=new Set([hashBits(orig)]); let evals=0;
 const offer=n=>{ board.push(n); board.sort((a,b)=>a.score-b.score); if(board.length>10) board.length=10; };
 const counts=[12,8,6];
 const nested=(node,d)=>{ if(d>=counts.length) return; const pool=stat.concat(juggleCandidates(node.bits,1,8,3,true)); const kids=[];
  for(const v of pool){ const r=applyVariant(node.bits,v); evals++; const h=hashBits(r.out); if(visited.has(h)) continue; visited.add(h);
   const ch={bits:r.out,hdr:node.hdr+r.hdr,recs:node.recs.concat([r.rec])}; ch.score=sc(r.out)+ch.hdr+32; offer(ch); kids.push(ch); }
  kids.sort((a,b)=>a.score-b.score); for(const k of kids.slice(0,counts[d])) nested(k,d+1); };
 const t0=Date.now(); nested({bits:orig,hdr:0,recs:[]},0);
 const P=b=>Math.round(b/orig.length*1e6).toLocaleString();
 console.log(`\n### ${label} | scored by ${coder==='context'?'bit context (12)':'balance only'} | ${evals.toLocaleString()} chains in ${((Date.now()-t0)/1000).toFixed(0)}s`);
 console.log(`  No transform: ${P(base)}`);
 board.slice(0,5).forEach((e,i)=>{ let x=e.bits; for(let j=e.recs.length-1;j>=0;j--) x=invertRec(x,e.recs[j]);
  console.log(`  ${i+1}. ${P(e.score)}  ${e.recs.map(labelOf).join(' > ')}  [decodes: ${eqBits(x,orig)}]`); });
}
const ex=makeExample(), txt=fs.readFileSync('/usr/lib/python3.12/pydoc_data/topics.py');
for(const [l,d] of [['Example gate log (16 KB)',ex],['Python docs text (16 KB)',txt]]) for(const c of ['balance','context']) run(l,d,c);
