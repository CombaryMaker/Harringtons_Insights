
// ===================== UI + SEARCH DRIVER =====================
const $ = id => document.getElementById(id);
const state = { data: null, name: '', running: false, stop: false, evals: 0, board: [], orig: null, sel: -1, base: null };

function makeExample() { // generated gate-log text, clearly an example
  let seed = 20261024; const rnd = () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296;
  const gates = ['gate-1', 'gate-2', 'gate-3', 'pool', 'clubhouse', 'east-ped'], res = ['granted', 'granted', 'granted', 'denied', 'held-open'];
  let s = '', t = 18 * 3600;
  while (s.length < 140000) {
    t += 1 + Math.floor(rnd() * 90);
    const hh = String(Math.floor(t / 3600) % 24).padStart(2, '0'), mm = String(Math.floor(t / 60) % 60).padStart(2, '0'), ss = String(t % 60).padStart(2, '0');
    const g = gates[Math.floor(rnd() * gates.length)], r = res[Math.floor(rnd() * res.length)];
    s += `2026-10-24 ${hh}:${mm}:${ss} INFO ${g} badge=${1000 + Math.floor(rnd() * 400)} result=${r}\n`;
  }
  return new TextEncoder().encode(s);
}
state.data = makeExample(); state.name = 'Example data: generated gate-log text';

function intList(s, max) { return [...new Set(s.split(/[^0-9]+/).filter(Boolean).map(Number).filter(n => n > 0 && n <= max))]; }
function checkedVals(cls) { return [...document.querySelectorAll('.' + cls)].filter(e => e.checked).map(e => +e.value); }

function readConfig() {
  const c = {
    sort: $('useSort').checked, sortM: checkedVals('sortM'), sortK: intList($('sortK').value, 255), sortFull: $('sortFull').checked,
    dirs: [$('dir1').checked ? 1 : null, $('dir0').checked ? 0 : null].filter(v => v !== null),
    tsort: $('useTSort').checked, tsortM: checkedVals('tsortM'), tsortK: intList($('tsortK').value, 242), tsortFull: $('tsortFull').checked,
    habits: $('useHabits').checked, habMode: $('habBest').checked ? 'best' : 'all',
    juggle: $('useJuggle').checked, jMin: Math.max(1, Math.min(8, +$('jMin').value || 1)), jMax: Math.max(1, Math.min(8, +$('jMax').value || 8)),
    jKeep: Math.max(1, Math.min(64, +$('jKeep').value || 3)), jPairs: $('jPairs').checked,
    mode: $('modeBeam').checked ? 'beam' : 'nested', counts: intList($('counts').value, 100000), pick: $('pick').value,
    beamW: Math.max(1, +$('beamW').value || 8), beamD: Math.max(1, +$('beamD').value || 5),
    coder: $('coder').value, ctxK: Math.max(4, Math.min(22, +$('ctxK').value || 12)),
    maxEvals: Math.max(100, +$('maxEvals').value || 200000), sampleKB: Math.max(1, Math.min(512, +$('sampleKB').value || 16)),
    templates: $('templates').value.split('\n').map(l => l.trim()).filter(Boolean)
  };
  if (c.jMin > c.jMax) [c.jMin, c.jMax] = [c.jMax, c.jMin];
  if (!c.counts.length) c.counts = [12, 8, 6];
  return c;
}
function staticVariants(c) {
  const v = [];
  if (c.sort) for (const m of c.sortM) for (const dir of c.dirs) {
    const N = 1 << m, ks = c.sortK.filter(k => k < N); if (c.sortFull) ks.push(N);
    for (const k of [...new Set(ks)]) v.push({ t: 'B', m, k, dir });
  }
  if (c.tsort) for (const m of c.tsortM) {
    const N = 3 ** m, ks = c.tsortK.filter(k => k < N); if (c.tsortFull) ks.push(N);
    for (const k of [...new Set(ks)]) v.push({ t: 'T', m, k });
  }
  if (c.habits) { if (c.habMode === 'all') { for (const cin of [0, 1]) for (let p = 0; p < 6; p++) v.push({ t: 'H', cin, p }); } else v.push({ t: 'H', best: true }); }
  return v;
}
function poolSize(c, slot) {
  const st = staticVariants(c).filter(v => slot === 'A' || v.t === slot).length;
  const j = c.juggle && (slot === 'A' || slot === 'J') ? c.jKeep + (c.jPairs ? 1 : 0) : 0;
  return st + j;
}
function plannedEvals(c) {
  const pats = c.templates.length ? c.templates.map(parsePattern) : [null];
  let total = 0;
  for (const pat of pats) {
    const D = pat ? pat.length : (c.mode === 'beam' ? c.beamD : c.counts.length);
    if (c.mode === 'beam') { for (let d = 0; d < D; d++) total += (d === 0 ? 1 : c.beamW) * poolSize(c, pat ? pat[d] : 'A'); }
    else { let nodes = 1; for (let d = 0; d < D; d++) { const P = poolSize(c, pat ? pat[d] : 'A'); total += nodes * P; nodes *= Math.min(P, c.counts[Math.min(d, c.counts.length - 1)]); } }
  }
  return total;
}
function updateEstimate() {
  try {
    const c = readConfig(), p = plannedEvals(c);
    $('estimate').textContent = `Planned: about ${Math.round(p).toLocaleString()} chains${p > c.maxEvals ? ` (capped at ${c.maxEvals.toLocaleString()})` : ''}. Each step offers about ${poolSize(c, 'A')} options.`;
    $('estimate').style.color = '';
  } catch (e) { $('estimate').textContent = e.message; $('estimate').style.color = 'var(--bad)'; }
}

const per1M = (bits) => Math.round(bits / state.orig.length * 1e6);
const fmt = n => Math.round(n).toLocaleString();

function scoreBits(b, c) { return c.coder === 'context' ? costContext(b, c.ctxK) : costBalance(b); }
function otherScore(b, c) { return c.coder === 'context' ? costBalance(b) : costContext(b, c.ctxK); }

async function runSearch(c) {
  const sample = state.data.subarray(0, c.sampleKB * 1024);
  state.orig = bytesToBits(sample);
  state.board = []; state.sel = -1; state.evals = 0; state.stop = false; state.running = true;
  const visited = new Set();
  const root = { bits: state.orig, hdr: 0, recs: [], score: scoreBits(state.orig, c) + 32 };
  state.base = { score: root.score, other: otherScore(state.orig, c) + 32, pure: pureBits(state.orig) };
  visited.add(hashBits(state.orig));
  const stat = staticVariants(c);
  const planned = Math.min(plannedEvals(c), c.maxEvals);
  $('sPlan').textContent = fmt(planned);
  const t0 = performance.now(); let lastYield = t0;
  const tick = async () => {
    const now = performance.now();
    if (now - lastYield > 40) {
      lastYield = now;
      const el = (now - t0) / 1000;
      $('sEvals').textContent = fmt(state.evals); $('sTime').textContent = el.toFixed(el < 10 ? 1 : 0) + ' s';
      $('sRate').textContent = fmt(state.evals / Math.max(el, 0.001)) + '/s';
      $('progress').style.width = Math.min(100, state.evals / Math.max(1, planned) * 100) + '%';
      renderBoard(c, false);
      await new Promise(r => setTimeout(r, 0));
    }
  };
  const offer = (node) => {
    const B = state.board;
    if (B.length >= 30 && node.score >= B[B.length - 1].score) return;
    B.push({ bits: node.bits, recs: node.recs, score: node.score, hdr: node.hdr, other: null, pure: null, ok: null });
    B.sort((a, b) => a.score - b.score); if (B.length > 30) B.length = 30;
  };
  const done = () => state.stop || state.evals >= c.maxEvals;
  const pool = (node, slot) => {
    let p = stat.filter(v => slot === 'A' || v.t === slot);
    if (c.juggle && (slot === 'A' || slot === 'J')) p = p.concat(juggleCandidates(node.bits, c.jMin, c.jMax, c.jKeep, c.jPairs));
    return p;
  };
  const expand = async (node, slot) => {
    const kids = [];
    for (const v0 of pool(node, slot)) {
      if (done()) break;
      let r;
      if (v0.t === 'H' && v0.best) { // pick the best of the 12 remaps by score, counted as one try
        let bestR = null, bestS = Infinity;
        for (const cin of [0, 1]) for (let p = 0; p < 6; p++) { const rr = habitsApply(node.bits, cin, p); const s = scoreBits(rr.out, c); if (s < bestS) { bestS = s; bestR = rr; } }
        r = bestR;
      } else r = applyVariant(node.bits, v0);
      state.evals++;
      const h = hashBits(r.out);
      if (visited.has(h)) { await tick(); continue; }
      if (visited.size > 3000000) visited.clear();
      visited.add(h);
      const child = { bits: r.out, hdr: node.hdr + r.hdr, recs: node.recs.concat([r.rec]) };
      child.score = scoreBits(r.out, c) + child.hdr + 32;
      offer(child); kids.push(child);
      await tick();
    }
    kids.sort((a, b) => a.score - b.score);
    return kids;
  };
  const choose = (kids, N) => {
    if (kids.length <= N) return kids;
    if (c.pick === 'best') return kids.slice(0, N);
    const shuffled = kids.slice(); for (let i = shuffled.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [shuffled[i], shuffled[j]] = [shuffled[j], shuffled[i]]; }
    if (c.pick === 'random') return shuffled.slice(0, N);
    const half = Math.ceil(N / 2), top = kids.slice(0, half), rest = shuffled.filter(k => !top.includes(k)).slice(0, N - half);
    return top.concat(rest);
  };
  const nested = async (node, d, pat) => {
    const D = pat ? pat.length : c.counts.length;
    if (d >= D || done()) return;
    const kids = await expand(node, pat ? pat[d] : 'A');
    const N = c.counts[Math.min(d, c.counts.length - 1)];
    for (const k of choose(kids, N)) { if (done()) return; await nested(k, d + 1, pat); }
  };
  const beam = async (pat) => {
    let cur = [root]; const D = pat ? pat.length : c.beamD;
    for (let d = 0; d < D && !done(); d++) {
      let all = [];
      for (const node of cur) { all = all.concat(await expand(node, pat ? pat[d] : 'A')); if (done()) break; }
      all.sort((a, b) => a.score - b.score); cur = all.slice(0, c.beamW);
      if (!cur.length) break;
    }
  };
  const pats = c.templates.length ? c.templates.map(parsePattern) : [null];
  for (const pat of pats) { if (done()) break; if (c.mode === 'beam') await beam(pat); else await nested(root, 0, pat); }
  lastYield = 0; await tick();
  state.running = false;
  $('progress').style.width = '100%';
  await verifyTop(c);
}

async function verifyTop(c) {
  for (const e of state.board) {
    if (e.ok !== null) continue;
    let x = e.bits; for (let i = e.recs.length - 1; i >= 0; i--) x = invertRec(x, e.recs[i]);
    e.ok = eqBits(x, state.orig);
    renderBoard(c, true); await new Promise(r => setTimeout(r, 0));
  }
}

function chipsHTML(recs) {
  if (!recs.length) return '<span class="chip H" style="color:var(--muted)">No transform</span>';
  return recs.map(r => `<span class="chip ${r.t}">${labelOf(r)}</span>`).join('<span class="arrow">›</span>');
}
function renderBoard(c, full) {
  const tb = $('board'); const B = state.board;
  $('thMain').textContent = (c.coder === 'context' ? 'Bit context' : 'Balance only') + ': 1,000,000 bits down to';
  $('thOther').textContent = c.coder === 'context' ? 'Balance only' : `Bit context (${c.ctxK})`;
  for (const e of B) { if (e.other === null && (full || !state.running)) { e.other = otherScore(e.bits, c) + e.hdr + 32; e.pure = pureBits(e.bits); } }
  let html = '';
  if (state.base) html += `<tr class="base clickable" data-i="-1"><td>–</td><td><div class="pipe">${chipsHTML([])}</div></td><td class="num">${fmt(per1M(state.base.score))}</td><td class="num">${fmt(per1M(state.base.other))}</td><td class="num">0</td><td class="num">${fmt(state.base.pure)}</td><td class="ok">✓</td></tr>`;
  B.forEach((e, i) => {
    const okCell = e.ok === null ? '<td class="pending">…</td>' : (e.ok ? '<td class="ok">✓</td>' : '<td class="no">✗</td>');
    html += `<tr class="clickable${i === state.sel ? ' sel' : ''}" data-i="${i}"><td>${i + 1}</td><td><div class="pipe">${chipsHTML(e.recs)}</div></td><td class="num">${fmt(per1M(e.score))}</td><td class="num">${e.other === null ? '…' : fmt(per1M(e.other))}</td><td class="num">${fmt(e.hdr)}</td><td class="num">${e.pure === null ? '…' : fmt(e.pure)}</td>${okCell}</tr>`;
  });
  tb.innerHTML = html;
  const top = B[0];
  if (top && state.base) {
    const better = top.score < state.base.score;
    $('bestBig').innerHTML = `1,000,000 bits down to ${fmt(per1M(top.score))} bits <small>${c.coder === 'context' ? 'bit-context coder' : 'balance-only coder'}</small>`;
    $('bestPipe').innerHTML = chipsHTML(top.recs);
    $('bestCmp').innerHTML = `No transform: <b>${fmt(per1M(state.base.score))}</b> bits. ${better ? `This chain saves <b>${fmt(per1M(state.base.score) - per1M(top.score))}</b> bits per 1,000,000.` : 'No chain has beaten leaving the data alone yet.'}`;
    if (state.sel === -1 && !state.running) selectRow(0, c);
  }
}
function drawStrip(canvas, bits) {
  const ctx = canvas.getContext('2d'), W = canvas.width, H = canvas.height, cs = getComputedStyle(document.documentElement);
  const hex = v => { v = cs.getPropertyValue(v).trim(); const m = v.match(/^#([0-9a-f]{6})$/i); const n = m ? parseInt(m[1], 16) : 0x888888; return [n >> 16, (n >> 8) & 255, n & 255]; };
  const z = hex('--zero'), mid = hex('--mid'), one = hex('--one');
  ctx.clearRect(0, 0, W, H);
  if (!bits || !bits.length) return;
  const n = bits.length, P = prefixOnes(bits);
  for (let x = 0; x < W; x++) {
    const s = Math.floor(x * n / W), e = Math.max(s + 1, Math.floor((x + 1) * n / W)), d = (P[Math.min(e, n)] - P[s]) / (Math.min(e, n) - s);
    const [a, b, t] = d < 0.5 ? [z, mid, d * 2] : [mid, one, (d - 0.5) * 2];
    ctx.fillStyle = `rgb(${a.map((v, i) => Math.round(v + (b[i] - v) * t)).join(',')})`;
    ctx.fillRect(x, 0, 1, H);
  }
}
function selectRow(i, c) {
  state.sel = i;
  drawStrip($('stripA'), state.orig);
  const e = i >= 0 ? state.board[i] : null;
  $('stripLabelB').textContent = e ? 'Selected chain: ' + (e.recs.map(labelOf).join(' › ') || 'No transform') : 'Selected chain: No transform';
  drawStrip($('stripB'), e ? e.bits : state.orig);
  document.querySelectorAll('#board tr').forEach(tr => tr.classList.toggle('sel', +tr.dataset.i === i));
}

// ---- wiring ----
$('fileName').textContent = state.name + ` (${state.data.length.toLocaleString()} bytes)`;
const loadFile = f => { const r = new FileReader(); r.onload = () => { state.data = new Uint8Array(r.result); state.name = f.name; $('fileName').textContent = `${f.name} (${state.data.length.toLocaleString()} bytes)`; }; r.readAsArrayBuffer(f); };
$('file').addEventListener('change', e => { if (e.target.files[0]) loadFile(e.target.files[0]); });
const drop = $('drop');
['dragenter', 'dragover'].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.classList.add('over'); }));
['dragleave', 'drop'].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.classList.remove('over'); }));
drop.addEventListener('drop', e => { const f = e.dataTransfer.files[0]; if (f) loadFile(f); });
document.querySelectorAll('input[name="mode"]').forEach(r => r.addEventListener('change', () => { $('nestedBox').hidden = $('modeBeam').checked; $('beamBox').hidden = !$('modeBeam').checked; updateEstimate(); }));
$('settings').addEventListener('input', updateEstimate);
$('settings').addEventListener('change', updateEstimate);
$('board').addEventListener('click', e => { const tr = e.target.closest('tr'); if (tr) selectRow(+tr.dataset.i, readConfig()); });
$('settings').addEventListener('submit', async e => {
  e.preventDefault(); if (state.running) return;
  let c; try { c = readConfig(); plannedEvals(c); } catch (err) { $('estimate').textContent = err.message; return; }
  $('runBtn').disabled = true; $('stopBtn').disabled = false; $('verifyBtn').disabled = true;
  $('bestBig').innerHTML = 'Searching… <small></small>'; $('bestPipe').innerHTML = ''; $('bestCmp').textContent = '';
  try { await runSearch(c); } catch (err) { $('bestCmp').textContent = 'Search stopped: ' + err.message; state.running = false; }
  $('runBtn').disabled = false; $('stopBtn').disabled = true; $('verifyBtn').disabled = false;
  renderBoard(c, true); selectRow(state.sel >= 0 ? state.sel : 0, c);
});
$('stopBtn').addEventListener('click', () => { state.stop = true; });
$('verifyBtn').addEventListener('click', async () => { const c = readConfig(); state.board.forEach(e => e.ok = null); renderBoard(c, true); await verifyTop(c); });
window.matchMedia('(prefers-color-scheme: dark)').addEventListener?.('change', () => selectRow(state.sel, readConfig()));
updateEstimate();
state.orig = bytesToBits(state.data.subarray(0, 16 * 1024));
drawStrip($('stripA'), state.orig); drawStrip($('stripB'), state.orig);
