
// ========================================================================
// hcm-trit engine: same model as HCM v3, but each byte is coded as its 6
// base-3 digits (trits). Each trit is coded as up to two yes/no
// decisions: first "is it the split-off value S?" (S chosen per digit by
// the swap pattern), then which of the other two values it is. Digits that
// cannot vary are skipped. Two digits share one 64-byte block per context.
// ========================================================================
namespace vt {

using namespace v3;   // tables, coder, buckets, hashing, mixer training

struct TTree {
    int c[256][2];       // children: >= 0 node, < 0 leaf (byte = -1 - child)
    int digit[256];      // which base-3 digit a node decides
    int segroot[256];    // node that starts this node's two-digit segment
    int slot[256];       // position inside the segment's block (1..8)
    int pnode[256][12], pbit[256][12], plen[256];
    int nn;
};

static int pow3(int k) { int r = 1; while (k-- > 0) r *= 3; return r; }

static int tt_new(TTree *t, int d) { int n = t->nn++; t->digit[n] = d; t->c[n][0] = t->c[n][1] = 0; return n; }

// values vals[0..nv-1] all share digits 0..d-1; split them on digit d
static int tt_build(TTree *t, const int *vals, int nv, int d, const int *split) {
    if (nv == 1) return -1 - vals[0];
    int g[3][256], gn[3] = {0, 0, 0};
    for (int i = 0; i < nv; i++) { int dg = (vals[i] / pow3(5 - d)) % 3; g[dg][gn[dg]++] = vals[i]; }
    int ne = (gn[0] > 0) + (gn[1] > 0) + (gn[2] > 0);
    if (ne == 1) { for (int k = 0; k < 3; k++) if (gn[k]) return tt_build(t, g[k], gn[k], d + 1, split); }
    if (ne == 2) {
        int k0 = -1, k1 = -1;
        for (int k = 0; k < 3; k++) if (gn[k]) { if (k0 < 0) k0 = k; else k1 = k; }
        int n = tt_new(t, d);
        int a = tt_build(t, g[k0], gn[k0], d + 1, split);
        int b = tt_build(t, g[k1], gn[k1], d + 1, split);
        t->c[n][0] = a; t->c[n][1] = b;
        return n;
    }
    int F = split[d], o1 = -1, o2 = -1;
    for (int k = 0; k < 3; k++) if (k != F) { if (o1 < 0) o1 = k; else o2 = k; }
    int n = tt_new(t, d);
    int a = tt_build(t, g[F], gn[F], d + 1, split);
    int m = tt_new(t, d);
    int b0 = tt_build(t, g[o1], gn[o1], d + 1, split);
    int b1 = tt_build(t, g[o2], gn[o2], d + 1, split);
    t->c[m][0] = b0; t->c[m][1] = b1;
    t->c[n][0] = m; t->c[n][1] = a;       // 1 = "it is the split-off value"
    return n;
}

static void tt_walk(TTree *t, int node, int *pn, int *pb, int len, int root, int *cnt) {
    int seg = t->digit[node] / 2;
    if (node == 0 || t->digit[root] / 2 != seg) { root = node; cnt[root] = 0; }
    t->segroot[node] = root;
    t->slot[node] = ++cnt[root];
    for (int b = 0; b < 2; b++) {
        pn[len] = node; pb[len] = b;
        int ch = t->c[node][b];
        if (ch < 0) { int v = -1 - ch; t->plen[v] = len + 1; for (int i = 0; i <= len; i++) { t->pnode[v][i] = pn[i]; t->pbit[v][i] = pb[i]; } }
        else tt_walk(t, ch, pn, pb, len + 1, root, cnt);
    }
}

// pattern: 6 digits, each 0..2 = the value split off first at that digit
static void tt_make(TTree *t, const int *split) {
    int vals[256];
    for (int v = 0; v < 256; v++) vals[v] = v;
    t->nn = 0;
    tt_build(t, vals, 256, 0, split);
    int pn[12], pb[12], cnt[256];
    tt_walk(t, 0, pn, pb, 0, 0, cnt);
}

template <int DEC, int TWOMIX>
static void run(Tables *t, const TTree *tr, uint8_t *buf, size_t n, Coder *cd) {
    const int bits = t->bits;
    uint32_t h[NH], *base[NH], *slot[NM];
    uint32_t word = 0, pword = 0;
    int64_t mptr = 0, mlen = 0;
    uint16_t mm[64];
    for (int i = 0; i < 64; i++) mm[i] = 49152;
#if defined(_MSC_VER)
    __declspec(align(16)) int32_t x[NINP];
#else
    int32_t x[NINP] __attribute__((aligned(16)));
#endif
    for (int i = 0; i < NINP; i++) x[i] = 0;
    int last = 0, last2 = 0;
    ctx_hashes(h, buf, 0, 0, 0);
    for (size_t pos = 0; pos < n; pos++) {
        int c = DEC ? 0 : buf[pos];
        int mbyte = (mlen > 0 && (size_t)mptr < pos) ? buf[mptr] : -1;
        int mk = mlen < 63 ? (int)mlen : 63;
        int ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        int letter = ((last | 32) >= 'a' && (last | 32) <= 'z') ? 1 : 0;
        int sel2 = cls(last) * 5 + cls(last2);
        int node = 0, step = 0;
        for (;;) {
            if (tr->segroot[node] == node) {   // entering a new two-digit segment
                uint32_t xs[NH];
                for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], (uint32_t)node + 1); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
            }
            int sl = tr->slot[node];
            for (int i = 0; i < NH; i++) slot[i] = base[i] + sl;
            slot[NH] = t->T0 + node;
            for (int i = 0; i < NM; i++) x[i] = STI[(*slot[i] & 0xFFFF) >> 4];
            int mexp = -1;
            if (mbyte >= 0) {
                if (step < tr->plen[mbyte] && tr->pnode[mbyte][step] == node) mexp = tr->pbit[mbyte][step];
                else mbyte = -1;
            }
            if (mexp >= 0) { int s = STI[mm[mk] >> 4]; x[NM] = mexp ? s : -s; } else x[NM] = 0;
            x[NM + 1] = 64;
            int32_t *ws = t->w + (size_t)(node + 256 * ms + 1024 * letter) * NINP;
            int64_t dot = 0;
            for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * x[i];
            int s, pm1 = 0, pm2 = 0;
            int32_t *ws2 = NULL;
            if (TWOMIX) {
                ws2 = t->w2 + (size_t)(node + 256 * sel2) * NINP;
                int64_t dot2 = 0;
                for (int i = 0; i < NIN; i++) dot2 += (int64_t)ws2[i] * x[i];
                int s1 = (int)(dot >> 16), s2 = (int)(dot2 >> 16);
                s1 = s1 > 2047 ? 2047 : (s1 < -2047 ? -2047 : s1);
                s2 = s2 > 2047 ? 2047 : (s2 < -2047 ? -2047 : s2);
                pm1 = SQI[s1 + 2048]; pm2 = SQI[s2 + 2048];
                s = (s1 + s2) >> 1;
            } else {
                s = (int)(dot >> 16);
            }
            s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
            int pm = SQI[s + 2048];
            int arow_i = (int)((uint32_t)node + 256 * (uint32_t)last);
            uint16_t *row = t->apm + (size_t)arow_i * 33;
            if (!t->apm_ok[arow_i]) { memcpy(row, t->row0, sizeof(t->row0)); t->apm_ok[arow_i] = 1; }
            int apos = s + 2048, j = apos >> 7, wt = apos & 127;
            int pa = (row[j] * (128 - wt) + row[j + 1] * wt) >> 11;
            int p = (pm * (256 - APMW) + pa * APMW) >> 8;
            p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
            int y;
            if (DEC) y = dec_bit(cd, (uint64_t)p);
            else y = tr->pbit[c][step], enc_bit(cd, y, (uint64_t)p);
            if (TWOMIX) { train(ws2, x, (y << 12) - pm2); train(ws, x, (y << 12) - pm1); }
            else train(ws, x, (y << 12) - pm);
            int tgt = y ? 65535 : 0;
            for (int i = 0; i < NM; i++) {
                uint32_t e = *slot[i];
                int pp = (int)(e & 0xFFFF), nn = (int)(e >> 16);
                pp += ((tgt - pp) * RT15[nn]) >> 15;
                nn = i == 0 ? NEXT60[nn] : NEXT255[nn];
                *slot[i] = (uint32_t)pp | ((uint32_t)nn << 16);
            }
            if (mexp >= 0) { int v = mm[mk]; int hit = y == mexp ? 65535 : 0; mm[mk] = (uint16_t)(v + ((hit - v) >> 5)); }
            int t16 = y ? 65535 : 0;
            row[j] = (uint16_t)(row[j] + (((t16 - row[j]) * (128 - wt)) >> APMRATE));
            row[j + 1] = (uint16_t)(row[j + 1] + (((t16 - row[j + 1]) * wt) >> APMRATE));
            int nx = tr->c[node][y];
            step++;
            if (nx < 0) { c = -1 - nx; break; }
            node = nx;
        }
        int byte = c;
        buf[pos] = (uint8_t)byte;
        size_t nb = pos + 1;
        if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)) word = (word * 0x2C9277B5u) ^ (uint32_t)(byte | 32);
        else { if (word) pword = word; word = 0; }
        if (mlen > 0 && (size_t)mptr < nb - 1 && buf[mptr] == byte) { mptr++; mlen++; }
        else mlen = 0;
        if (nb >= 6) {
            uint32_t hh = 0;
            for (int j2 = 1; j2 <= 6; j2++) hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + buf[nb - j2]) & 0xFFFFF);
            hh &= t->mmask;
            if (mlen == 0) {
                int64_t cand = t->mtab[hh];
                if (cand > 0) {
                    int L = 0;
                    while (L < 32 && cand - 1 - L >= 0 && buf[cand - 1 - L] == buf[nb - 1 - L]) L++;
                    if (L >= 6) { mptr = cand; mlen = L; }
                }
            }
            t->mtab[hh] = (int32_t)nb;
        }
        last2 = last;
        last = byte;
        ctx_hashes(h, buf, nb, word, pword);
    }
}

}  // namespace vt
