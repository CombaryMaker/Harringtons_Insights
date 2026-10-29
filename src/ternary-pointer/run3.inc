// Ternary pointer ("2 = pointer"), model-informed, with cached bit work.
// Before a byte, if there is a candidate byte (the match model's guess, or
// else the last byte seen after the same 4-byte context), one yes/no
// "is it the candidate?" decision is coded.  Its chance starts from the
// model's own chance of the candidate (product of the eight bit chances
// along its path) and is refined by a small mixer that also sees an escape
// table (match length or repeat count, plus recent outcomes) and a table
// keyed by the order-2 context and the candidate.  On "yes" the eight bit
// predictions already made are reused for learning only.  On "no" the
// candidate is ruled out: while the bits still follow its path, each bit
// chance has the candidate's share removed, and the last such bit is
// forced.  The model learns the same bits as plain v3.
#ifndef PTR
#define PTR 1
#endif
#ifndef PMINL
#define PMINL 6
#endif
#ifndef SR
#define SR 1
#endif
#ifndef SRH
#define SRH 3
#endif
#ifndef SRCAP
#define SRCAP 15
#endif
#ifndef ESH
#define ESH 11
#endif
#ifndef ELIM
#define ELIM 60
#endif
#ifndef PLIM
#define PLIM 255
#endif
#ifndef PFA
#define PFA 1
#endif
#ifndef GATE
#define GATE 0
#endif
#ifndef GATEM
#define GATEM 0
#endif
#ifndef LAZY
#define LAZY 0
#endif
#ifndef EQS
#define EQS 4
#endif
#ifndef EQ2INIT
#define EQ2INIT 49152
#endif
#ifndef SRBITS
#define SRBITS 22
#endif

struct BitSt {
    int32_t x[NINP] __attribute__((aligned(16)));
    uint32_t *slot[NM];
    int32_t *ws, *ws2;
    uint16_t *row;
    int pm, pm1, pm2, p, j, wt, mexp;
};

template <int TWOMIX>
__attribute__((always_inline)) static inline void predict(Tables *t, uint32_t *const *base, uint32_t c0, uint32_t nib, int mexp, int mmv,
                           int ms, int letter, int last, int sel2, BitSt *S) {
    for (int i = 0; i < NH; i++) S->slot[i] = base[i] + nib;
    S->slot[NH] = t->T0 + c0;
    for (int i = 0; i < NM; i++) S->x[i] = STI[(*S->slot[i] & 0xFFFF) >> 4];
    S->mexp = mexp;
    if (mexp >= 0) { int s = STI[mmv >> 4]; S->x[NM] = mexp ? s : -s; } else S->x[NM] = 0;
    S->x[NM + 1] = 64;
    S->ws = t->w + (size_t)(c0 + 256 * ms + 1024 * letter) * NINP;
    int64_t dot = 0;
    for (int i = 0; i < NIN; i++) dot += (int64_t)S->ws[i] * S->x[i];
    int s;
    if (TWOMIX) {
        S->ws2 = t->w2 + (size_t)(c0 + 256 * sel2) * NINP;
        int64_t dot2 = 0;
        for (int i = 0; i < NIN; i++) dot2 += (int64_t)S->ws2[i] * S->x[i];
        int s1 = (int)(dot >> 16), s2 = (int)(dot2 >> 16);
        s1 = s1 > 2047 ? 2047 : (s1 < -2047 ? -2047 : s1);
        s2 = s2 > 2047 ? 2047 : (s2 < -2047 ? -2047 : s2);
        S->pm1 = SQI[s1 + 2048]; S->pm2 = SQI[s2 + 2048];
        s = (s1 + s2) >> 1;
    } else s = (int)(dot >> 16);
    s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
    S->pm = SQI[s + 2048];
    int arow_i = (int)(c0 + 256 * (uint32_t)last);
    uint16_t *row = t->apm + (size_t)arow_i * 33;
    if (!t->apm_ok[arow_i]) { memcpy(row, t->row0, sizeof(t->row0)); t->apm_ok[arow_i] = 1; }
    S->row = row;
    int apos = s + 2048;
    S->j = apos >> 7; S->wt = apos & 127;
    int pa = (row[S->j] * (128 - S->wt) + row[S->j + 1] * S->wt) >> 11;
    int p = (S->pm * (256 - APMW) + pa * APMW) >> 8;
    S->p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
}

template <int TWOMIX>
__attribute__((always_inline)) static inline void learn(BitSt *S, int y, uint16_t *mmk) {
    if (TWOMIX) { train(S->ws2, S->x, (y << 12) - S->pm2); train(S->ws, S->x, (y << 12) - S->pm1); }
    else train(S->ws, S->x, (y << 12) - S->pm);
    int tgt = y ? 65535 : 0;
    for (int i = 0; i < NM; i++) {
        uint32_t e = *S->slot[i];
        int pp = (int)(e & 0xFFFF), nn = (int)(e >> 16);
        pp += ((tgt - pp) * RT15[nn]) >> 15;
        nn = i == 0 ? NEXT60[nn] : NEXT255[nn];
        *S->slot[i] = (uint32_t)pp | ((uint32_t)nn << 16);
    }
    if (S->mexp >= 0) { int v = *mmk; int hit = y == S->mexp ? 65535 : 0; *mmk = (uint16_t)(v + ((hit - v) >> 5)); }
    uint16_t *row = S->row;
    int j = S->j, wt = S->wt;
    row[j] = (uint16_t)(row[j] + (((tgt - row[j]) * (128 - wt)) >> APMRATE));
    row[j + 1] = (uint16_t)(row[j + 1] + (((tgt - row[j + 1]) * wt) >> APMRATE));
}

static inline int pr16(uint32_t v) { return (int)(v & 0xFFFF) >> 4; }
static inline void adapt(uint32_t *v, int e, int lim) {
    int pp = (int)(*v & 0xFFFF), nn = (int)(*v >> 16);
    pp += (((e ? 65535 : 0) - pp) * RT15[nn]) >> 15;
    nn = nn < lim ? nn + 1 : nn;
    *v = (uint32_t)pp | ((uint32_t)nn << 16);
}

template <int DEC, int TWOMIX>
static void run(Tables *t, uint8_t *buf, size_t n, Coder *cd) {
    const int bits = t->bits;
    uint32_t h[NH], *base[NH], *bB[NH];
    uint32_t word = 0, pword = 0;
    int64_t mptr = 0, mlen = 0;
    uint16_t mm[64];
    for (int i = 0; i < 64; i++) mm[i] = 49152;
    int last = 0, last2 = 0;
    // pointer state
    static uint32_t EP[256], EP3[65536];
    static int32_t EW[256][4];
    static uint32_t EQ2[256];
    for (int i = 0; i < 256; i++) EQ2[i] = EQ2INIT;
    static uint16_t SRT[1 << SRBITS];
    for (int i = 0; i < 256; i++) { EP[i] = 0x8000; EW[i][0] = 65536; EW[i][1] = EW[i][2] = EW[i][3] = 0; }
    for (int i = 0; i < 65536; i++) EP3[i] = 0x8000;
    if (SR) memset(SRT, 0, sizeof(SRT));
    int ehist = 0, ehist2 = 0, gcand = 0, gsrc = 0;
    BitSt st[8], tmp;
    ctx_hashes(h, buf, 0, 0, 0);
    for (size_t pos = 0; pos < n; pos++) {
        int c = DEC ? 0 : buf[pos];
        int mbyte = (mlen > 0 && (size_t)mptr < pos) ? buf[mptr] : -1;   // byte the match expects
        int mk = mlen < 63 ? (int)mlen : 63;
        int ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        int letter = ((last | 32) >= 'a' && (last | 32) <= 'z') ? 1 : 0;
        int sel2 = cls(last) * 5 + cls(last2);
        uint32_t c0 = 1, nib = 1;
        {
            uint32_t xs[NH];
            for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], 1); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
            for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
        }
        uint32_t sri = (h[SRH] >> (32 - SRBITS));
        int srv = SR ? SRT[sri] : 0;
        int cand = -1, src = 0;
        if (PTR) {
            if (mbyte >= 0 && mlen >= PMINL) cand = mbyte;
            else if (SR && mbyte < 0 && srv) { cand = srv & 255; src = 1; }
        }
        int code = 1;      // 0 = this byte came from the pointer (the trit was 2)
        int onpath = 0;    // bits so far follow the candidate's path (cached work valid)
        int xb = -1;       // candidate ruled out by a "no"
        uint32_t R[8];     // chance (16-bit) of finishing the candidate below each bit
        int q[8], lctx = 0;
        int gctx = -1;   // pointer skipped by the gate: its table cell still learns afterwards
        if (cand >= 0 && GATE) {
            int ectx0 = src ? 128 + ((srv >> 8) & 31) * 4 + ehist2 : (mlen < 31 ? (int)mlen : 31) * 4 + ehist;
            if (pr16(EP[ectx0]) < (src ? GATE : GATEM)) { gctx = ectx0; gcand = cand; gsrc = src; cand = -1; }
        }
        if (cand >= 0) {
            if (!LAZY) {
                uint32_t xs[NH], cc = 16 | (uint32_t)(cand >> 4);
                for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], cc); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                for (int i = 0; i < NH; i++) bB[i] = bucket_find(t->T[i], xs[i], bits);
            }
            const int lowbit = LAZY ? 4 : 0;
            if (PFA) for (int bit = 7; bit >= lowbit; bit--) prefetch(t->apm + (size_t)(((uint32_t)(cand | 256) >> (bit + 1)) + 256 * (uint32_t)last) * 33);
            uint64_t P = (uint64_t)1 << 30;
            int ectx = src ? 128 + ((srv >> 8) & 31) * 4 + ehist2 : (mlen < 31 ? (int)mlen : 31) * 4 + ehist;
            lctx = ectx;
            for (int bit = 7; bit >= lowbit; bit--) {
                uint32_t cc = (uint32_t)((cand | 256) >> (bit + 1));
                uint32_t nb2 = bit >= 4 ? cc : (uint32_t)(((cand & 15) | 16) >> (bit + 1));
                int b = (cand >> bit) & 1;
                predict<TWOMIX>(t, bit >= 4 ? base : bB, cc, nb2, src ? -1 : b, mm[mk], ms, letter, last, sel2, &st[bit]);
                q[bit] = b ? st[bit].p : 4096 - st[bit].p;
                P = (P * (uint64_t)q[bit]) >> 12;
            }
            if (LAZY) {   // second nibble not predicted yet: use its learned typical chance
                uint32_t q2 = EQ2[ectx] < 16 ? 16 : EQ2[ectx];
                R[4] = q2;
                P = (P * q2) >> 16;
                for (int k = 5; k < 8; k++) R[k] = (uint32_t)(((uint64_t)R[k - 1] * (uint64_t)q[k - 1]) >> 12);
            } else {
                R[0] = 65536;
                for (int k = 1; k < 8; k++) R[k] = (uint32_t)(((uint64_t)R[k - 1] * (uint64_t)q[k - 1]) >> 12);
            }
            int P12 = (int)(P >> 18);
            P12 = P12 < 1 ? 1 : (P12 > 4095 ? 4095 : P12);
            int i3 = (int)((h[1] * 0x9E3779B1u + (uint32_t)cand * 0x85EBCA6Bu + (uint32_t)src * 0x27D4EB2Fu) >> 16);
            int pe0 = (int)(EP[ectx] & 0xFFFF) >> 4;
            int pe3 = (int)(EP3[i3] & 0xFFFF) >> 4;
            int xe[4] = {STI[P12], STI[pe0], 256, STI[pe3]};
            int64_t ed = 0;
            for (int k = 0; k < 4; k++) ed += (int64_t)EW[ectx][k] * xe[k];
            int es = (int)(ed >> 16);
            es = es > 2047 ? 2047 : (es < -2047 ? -2047 : es);
            int pe = SQI[es + 2048];
            int e;
            if (DEC) e = dec_bit(cd, (uint64_t)pe);
            else { e = c == cand; enc_bit(cd, e, (uint64_t)pe); }
            int err = (e << 12) - pe;
            for (int k = 0; k < 4; k++) EW[ectx][k] += (xe[k] * err) >> ESH;
            adapt(&EP[ectx], e, PLIM);
            adapt(&EP3[i3], e, ELIM);
            if (src) ehist2 = ((ehist2 << 1) | e) & 3; else ehist = ((ehist << 1) | e) & 3;
            npt++; if (e) nhit++;
            if (e) { c = cand; code = 0; }
            else xb = cand;
            onpath = 1;
        }
        for (int bit = 7; bit >= 0; bit--) {
            if (bit == 3) {   // second nibble: new blocks, chosen by the first nibble
                nib = 1;
                if (onpath && LAZY) {   // still on the candidate: predict its second nibble now
                    uint32_t xs[NH];
                    for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], c0); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                    if (PFA) for (int b2 = 3; b2 >= 0; b2--) prefetch(t->apm + (size_t)(((uint32_t)(cand | 256) >> (b2 + 1)) + 256 * (uint32_t)last) * 33);
                    for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
                    uint32_t Q = 65536;
                    for (int b2 = 3; b2 >= 0; b2--) {
                        uint32_t cc = (uint32_t)((cand | 256) >> (b2 + 1));
                        uint32_t nb2 = (uint32_t)(((cand & 15) | 16) >> (b2 + 1));
                        int b = (cand >> b2) & 1;
                        predict<TWOMIX>(t, base, cc, nb2, src ? -1 : b, mm[mk], ms, letter, last, sel2, &st[b2]);
                        q[b2] = b ? st[b2].p : 4096 - st[b2].p;
                        Q = (uint32_t)(((uint64_t)Q * (uint64_t)q[b2]) >> 12);
                    }
                    R[0] = 65536;
                    for (int k = 1; k < 4; k++) R[k] = (uint32_t)(((uint64_t)R[k - 1] * (uint64_t)q[k - 1]) >> 12);
                    int qv = (int)EQ2[lctx];
                    EQ2[lctx] = (uint32_t)(qv + (((int)(Q > 65535 ? 65535 : Q) - qv) >> EQS));
                }
                else if (onpath) { for (int i = 0; i < NH; i++) base[i] = bB[i]; }
                else {
                    uint32_t xs[NH];
                    for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], c0); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                    for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
                }
            }
            BitSt *S;
            if (onpath) S = &st[bit];
            else {
                int mexp = -1;
                if (mbyte >= 0) {
                    if ((uint32_t)((mbyte | 256) >> (bit + 1)) == c0) mexp = (mbyte >> bit) & 1;
                    else mbyte = -1;
                }
                predict<TWOMIX>(t, base, c0, nib, mexp, mm[mk], ms, letter, last, sel2, &tmp);
                if (bit > 0) prefetch(t->apm + (size_t)(2 * c0 + 256 * (uint32_t)last) * 33);
                S = &tmp;
            }
            int y;
            if (!code) y = (c >> bit) & 1;
            else {
                int p = S->p, forced = -1;
                if (onpath) {   // still on the ruled-out candidate's path
                    int b = (xb >> bit) & 1;
                    uint32_t Rk = R[bit];
                    if (Rk >= 65536) forced = 1 - b;
                    else {
                        uint64_t pb = (uint64_t)(b ? p : 4096 - p);
                        uint64_t num = pb * (65536 - Rk) * 4096, den = (uint64_t)4096 * 65536 - pb * Rk;
                        int pb2 = (int)(num / den);
                        pb2 = pb2 < 1 ? 1 : (pb2 > 4095 ? 4095 : pb2);
                        p = b ? pb2 : 4096 - pb2;
                    }
                }
                if (forced >= 0) y = forced;
                else if (DEC) y = dec_bit(cd, (uint64_t)p);
                else { y = (c >> bit) & 1; enc_bit(cd, y, (uint64_t)p); }
            }
            learn<TWOMIX>(S, y, &mm[mk]);
            if (onpath && y != ((cand >> bit) & 1)) { onpath = 0; mbyte = -1; }
            c0 = (c0 << 1) | (uint32_t)y;
            nib = (nib << 1) | (uint32_t)y;
        }
        int byte = (int)(c0 & 255);
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
                int64_t cand2 = t->mtab[hh];
                if (cand2 > 0) {
                    int L = 0;
                    while (L < 32 && cand2 - 1 - L >= 0 && buf[cand2 - 1 - L] == buf[nb - 1 - L]) L++;
                    if (L >= 6) { mptr = cand2; mlen = L; }
                }
            }
            t->mtab[hh] = (int32_t)nb;
        }
        if (SR) {
            if (srv && (srv & 255) == byte) { int k = (srv >> 8) & 31; SRT[sri] = (uint16_t)(byte | ((k < SRCAP ? k + 1 : k) << 8)); }
            else SRT[sri] = (uint16_t)(byte | (1 << 8));
        }
        last2 = last;
        last = byte;
        if (gctx >= 0) {
            int e = byte == gcand;
            adapt(&EP[gctx], e, PLIM);
            if (gsrc) ehist2 = ((ehist2 << 1) | e) & 3; else ehist = ((ehist << 1) | e) & 3;
        }
        ctx_hashes(h, buf, nb, word, pword);
    }
}

