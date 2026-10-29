// Ternary <-> binary conversion transforms, fully reversible.
//   A: bytes -> 6 base-3 digits per byte -> value swap -> HaBiTS bits (2->0, 0->10, 1->11) -> bytes
//   B: bytes -> bits -> HaBiTS parse (0->2, 10->0, 11->1) -> value swap -> 5 trits per byte (base 3) -> bytes
// usage: ttx enc|dec A|B PERM in out      PERM = three digits, the new value of 0, 1, 2 (e.g. 102)
// Output starts with the original length (4 bytes) and the trit count (4 bytes).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
typedef std::vector<uint8_t> V;

static V readf(const char *p) { V d; FILE *f = fopen(p, "rb"); int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); fclose(f); return d; }
static void writef(const char *p, const V &d) { FILE *f = fopen(p, "wb"); fwrite(d.data(), 1, d.size(), f); fclose(f); }
static void put32(V &o, uint32_t v) { for (int k = 3; k >= 0; k--) o.push_back((uint8_t)(v >> (8 * k))); }
static uint32_t get32(const V &d, size_t i) { return ((uint32_t)d[i] << 24) | ((uint32_t)d[i + 1] << 16) | ((uint32_t)d[i + 2] << 8) | d[i + 3]; }

static V bits_to_bytes(const V &b) { V o((b.size() + 7) / 8, 0); for (size_t i = 0; i < b.size(); i++) if (b[i]) o[i >> 3] |= (uint8_t)(0x80 >> (i & 7)); return o; }
static V bytes_to_bits(const uint8_t *d, size_t n) { V b; b.reserve(n * 8); for (size_t i = 0; i < n; i++) for (int k = 7; k >= 0; k--) b.push_back((d[i] >> k) & 1); return b; }
static void habits_encode(const V &t, V &bits) { for (uint8_t v : t) { if (v == 2) bits.push_back(0); else { bits.push_back(1); bits.push_back(v == 1); } } }
static V habits_parse(const V &bits) {   // a lone trailing 1 is read as 10
    V t; for (size_t i = 0; i < bits.size();) { if (!bits[i]) { t.push_back(2); i++; } else { int nx = i + 1 < bits.size() ? bits[i + 1] : 0; t.push_back(nx ? 1 : 0); i += 2; } }
    return t;
}

int main(int argc, char **argv) {
    if (argc != 6) { fprintf(stderr, "usage: ttx enc|dec A|B PERM in out\n"); return 1; }
    int enc = argv[1][0] == 'e'; char m = argv[2][0];
    int P[3], Q[3];
    for (int i = 0; i < 3; i++) { P[i] = argv[3][i] - '0'; Q[P[i]] = i; }
    V in = readf(argv[4]), out;
    if (enc) {
        uint32_t n = (uint32_t)in.size();
        V t;
        if (m == 'A') { for (uint8_t b : in) { int v = b, dg[6]; for (int k = 5; k >= 0; k--) { dg[k] = v % 3; v /= 3; } for (int k = 0; k < 6; k++) t.push_back((uint8_t)dg[k]); } }
        else t = habits_parse(bytes_to_bits(in.data(), in.size()));
        for (auto &v : t) v = (uint8_t)P[v];
        put32(out, n); put32(out, (uint32_t)t.size());
        if (m == 'A') { V bits; habits_encode(t, bits); V by = bits_to_bytes(bits); out.insert(out.end(), by.begin(), by.end()); }
        else { for (size_t i = 0; i < t.size(); i += 5) { int v = 0; for (int k = 0; k < 5; k++) v = v * 3 + (i + k < t.size() ? t[i + k] : 0); out.push_back((uint8_t)v); } }
    } else {
        uint32_t n = get32(in, 0), m3 = get32(in, 4);
        V t;
        if (m == 'A') { V bits = bytes_to_bits(in.data() + 8, in.size() - 8); t = habits_parse(bits); t.resize(m3); }
        else { for (size_t i = 8; i < in.size(); i++) { int v = in[i], dg[5]; for (int k = 4; k >= 0; k--) { dg[k] = v % 3; v /= 3; } for (int k = 0; k < 5; k++) t.push_back((uint8_t)dg[k]); } t.resize(m3); }
        for (auto &v : t) v = (uint8_t)Q[v];
        if (m == 'A') { for (size_t i = 0; i < n; i++) { int v = 0; for (int k = 0; k < 6; k++) v = v * 3 + t[i * 6 + k]; out.push_back((uint8_t)v); } }
        else { V bits; habits_encode(t, bits); bits.resize((size_t)n * 8); out = bits_to_bytes(bits); }
    }
    writef(argv[5], out);
    return 0;
}
