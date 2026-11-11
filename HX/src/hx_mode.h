// hx_mode.h - Mode context grouping for HX (route / mode 19), 10 Nov 2026. Michael Hugh Harrington.
// The mode idea itself is public domain (CC0 1.0, github.com/CombaryMaker/Harringtons_Insights/tree/main/mode-context);
// this file is part of HX and has HX's license.
// Text switches between modes written differently. Two layouts are recognised:
//   scheme 1, books: paragraphs (lines up to a blank line) that open with a quote mark (dialogue) vs the rest;
//   scheme 2, Wikipedia-style markup: each line's kind - prose, heading, list, template/table, indented, category or
//   language link, escaped tag, line in a References / External links / See also / Notes section, blank.
// The units of each kind are moved together (each kind keeps its own order); the kind of every unit is stored with a
// small adaptive binary coder (context: the previous two kinds) so the order can be restored exactly. The grouped
// text is then compressed by the normal HX pipeline; the route is kept only when the total is smaller.
#pragma once
namespace hxmode {
static const int K1 = 2, K2 = 9;
// ---- unit splitting (must give the same units again on the grouped stream) ----
static bool blank_line(const uint8_t *s, size_t a, size_t b) { for (size_t i = a; i < b; i++) if (s[i] != '\r' && s[i] != ' ' && s[i] != '\t') return false; return true; }
// lines: each unit ends just after '\n'; a last line without '\n' is the tail
static void split_lines(const uint8_t *s, size_t n, vector<size_t> &st, size_t &body) { st.clear(); size_t a = 0; for (size_t i = 0; i < n; i++) if (s[i] == '\n') { st.push_back(a); a = i + 1; } body = a; }
// paragraphs: blank lines*, non-blank lines+, then the first blank line closes the unit; what is left is the tail
static void split_paras(const uint8_t *s, size_t n, vector<size_t> &st, size_t &body) {
    st.clear(); size_t ustart = 0, ls = 0; bool seen = false; body = 0;
    for (size_t i = 0; i < n; i++) if (s[i] == '\n') { bool bl = blank_line(s, ls, i);
        if (!bl) seen = true; else if (seen) { st.push_back(ustart); ustart = i + 1; body = ustart; seen = false; }
        ls = i + 1; } }
static bool starts_quote(const uint8_t *s, size_t a, size_t b) { while (a < b && (s[a] == '\r' || s[a] == '\n' || s[a] == ' ')) a++;
    if (a < b && s[a] == '"') return true; return a + 2 < b && s[a] == 0xE2 && s[a + 1] == 0x80 && (s[a + 2] == 0x9C || s[a + 2] == 0x98); }
static bool has(const uint8_t *s, size_t a, size_t b, const char *w) { size_t L = strlen(w); for (size_t i = a; i + L <= b; i++) if (!memcmp(s + i, w, L)) return true; return false; }
static int wiki_kind(const uint8_t *s, size_t a, size_t b, bool &refsec) {
    size_t e = b; while (e > a && (s[e - 1] == '\n' || s[e - 1] == '\r')) e--;
    if (e > a && s[a] == '=') { refsec = has(s, a, e, "eference") || has(s, a, e, "xternal link") || has(s, a, e, "ee also") || has(s, a, e, "Notes") || has(s, a, e, "urther reading") || has(s, a, e, "ibliograph") || has(s, a, e, "Sources"); return 1; }
    if (has(s, a, e, "<page>") || has(s, a, e, "<title>")) { refsec = false; return 4; }
    if (blank_line(s, a, e)) return 8; if (refsec) return 7;
    uint8_t c = s[a]; if (c == '*' || c == '#' || c == ':' || c == ';') return 2; if (c == '{' || c == '|' || c == '}' || c == '!') return 3; if (c == ' ') return 4;
    if (e - a > 2 && c == '[' && s[a + 1] == '[' && s[a + 2] >= 'a' && s[a + 2] <= 'z') return 5; if (e - a > 3 && !memcmp(s + a, "&lt;", 4)) return 6; return 0; }
// ---- kinds coder: adaptive binary arithmetic coder, each kind as 4 bits, context = previous two kinds + bits so far ----
struct KC { vector<uint16_t> p; KC() : p(10 * 10 * 16, 2048) {} };
static B enc_kinds(const vector<uint8_t> &k) {
    KC m; B o; uint32_t x1 = 0, x2 = 0xffffffff; int a = 9, b = 9;
    for (uint8_t v : k) { int ctx = 1; for (int j = 3; j >= 0; j--) { int y = v >> j & 1; uint16_t &pr = m.p[(a * 10 + b) * 16 + ctx];
            uint32_t xm = x1 + (uint32_t)(((uint64_t)(x2 - x1) * pr) >> 12); if (y) x2 = xm; else x1 = xm + 1;
            if (y) pr += (4096 - pr) >> 4; else pr -= pr >> 4; ctx = ctx * 2 + y;
            while (((x1 ^ x2) & 0xff000000) == 0) { o.push_back(x2 >> 24); x1 <<= 8; x2 = (x2 << 8) | 255; } }
        a = b; b = v; }
    for (int i = 0; i < 4; i++) { o.push_back(x1 >> 24); x1 <<= 8; } return o; }
static bool dec_kinds(const uint8_t *s, size_t n, size_t cnt, vector<uint8_t> &k) {
    KC m; uint32_t x1 = 0, x2 = 0xffffffff, x = 0; size_t pos = 0; int a = 9, b = 9; auto in = [&]() -> uint32_t { return pos < n ? s[pos++] : 0; };
    for (int i = 0; i < 4; i++) x = (x << 8) | in(); k.resize(cnt);
    for (size_t u = 0; u < cnt; u++) { int ctx = 1;
        for (int j = 3; j >= 0; j--) { uint16_t &pr = m.p[(a * 10 + b) * 16 + ctx]; uint32_t xm = x1 + (uint32_t)(((uint64_t)(x2 - x1) * pr) >> 12);
            int y = x <= xm; if (y) x2 = xm; else x1 = xm + 1; if (y) pr += (4096 - pr) >> 4; else pr -= pr >> 4; ctx = ctx * 2 + y;
            while (((x1 ^ x2) & 0xff000000) == 0) { x1 <<= 8; x2 = (x2 << 8) | 255; x = (x << 8) | in(); } }
        int v = ctx - 16; if (v > 9) return false; k[u] = (uint8_t)v; a = b; b = v; }
    return true; }
// ---- grouping ----
static void unit_bounds(const vector<size_t> &st, size_t body, size_t u, size_t &a, size_t &b) { a = st[u]; b = u + 1 < st.size() ? st[u + 1] : body; }
static B ungroup(int scheme, const B &g, const vector<uint8_t> &kinds, size_t tail) {
    if (tail > g.size()) return B(); size_t gb = g.size() - tail; vector<size_t> st; size_t body;
    if (scheme == 1) split_paras(g.data(), gb, st, body); else split_lines(g.data(), gb, st, body);
    if (body != gb || st.size() != kinds.size()) return B();
    int K = scheme == 1 ? K1 : K2; vector<size_t> cnt(K, 0), start(K, 0), used(K, 0); for (uint8_t v : kinds) { if (v >= K) return B(); cnt[v]++; }
    for (int k = 1; k < K; k++) start[k] = start[k - 1] + cnt[k - 1];
    B out; out.reserve(g.size());
    for (uint8_t v : kinds) { size_t u = start[v] + used[v]++, a, b; unit_bounds(st, body, u, a, b); out.insert(out.end(), g.begin() + a, g.begin() + b); }
    out.insert(out.end(), g.begin() + gb, g.end()); return out; }
// Builds the grouped text and the kinds; scheme 0 = this file does not look like either layout.
static int group(const B &in, vector<uint8_t> &kinds, B &g, size_t &tail) {
    size_t n = in.size(); if (n < 65536 || getenv("HX_NOMODE")) return 0;
    size_t text = 0; for (size_t i = 0; i < n; i += 61) { uint8_t c = in[i]; text += (c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t' || c >= 0xC2; }
    if (text * 61 < n * 95 / 100) return 0;   // not text
    vector<size_t> st; size_t body; int scheme = 0;
    // Wikipedia-style markup?
    split_lines(in.data(), n, st, body); { size_t marked = 0; bool rs = false; for (size_t u = 0; u < st.size(); u++) { size_t a, b; unit_bounds(st, body, u, a, b); int k = wiki_kind(in.data(), a, b, rs); marked += k == 1 || k == 3 || k == 5 || k == 7; }
        if (st.size() >= 200 && marked * 20 >= st.size()) scheme = 2; }
    if (!scheme) { split_paras(in.data(), n, st, body); size_t q = 0; for (size_t u = 0; u < st.size(); u++) { size_t a, b; unit_bounds(st, body, u, a, b); q += starts_quote(in.data(), a, b); }
        if (st.size() >= 100 && q * 10 >= st.size() && q * 10 <= st.size() * 9) scheme = 1; }
    if (!scheme) return 0;
    if (scheme == 2) split_lines(in.data(), n, st, body);
    kinds.assign(st.size(), 0); bool rs = false;
    for (size_t u = 0; u < st.size(); u++) { size_t a, b; unit_bounds(st, body, u, a, b); kinds[u] = scheme == 1 ? (starts_quote(in.data(), a, b) ? 0 : 1) : (uint8_t)wiki_kind(in.data(), a, b, rs); }
    int K = scheme == 1 ? K1 : K2; g.clear(); g.reserve(n);
    for (int k = 0; k < K; k++) for (size_t u = 0; u < st.size(); u++) if (kinds[u] == k) { size_t a, b; unit_bounds(st, body, u, a, b); g.insert(g.end(), in.begin() + a, in.begin() + b); }
    g.insert(g.end(), in.begin() + body, in.end()); tail = n - body;
    if (ungroup(scheme, g, kinds, tail) != in) return 0;   // must come back exactly, or the route is not used
    return scheme; }
}
