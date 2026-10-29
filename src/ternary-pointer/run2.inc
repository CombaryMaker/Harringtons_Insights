// Ternary pointer, model-informed version.
// Before each byte where the match model has a guess (mbyte), the coder
// first codes one "trit-2" decision: is this byte exactly the pointed-to
// byte?  Its probability starts from the model's own chance of mbyte (the
// product of the eight bit chances along mbyte's path), refined by a small
// mixer that also sees an escape table indexed by match length and recent
// pointer outcomes.  On "yes" the byte is known and the bits are only
// learned, not coded.  On "no" the byte is coded bit by bit with mbyte
// excluded (its share is removed from the bit chances), so nothing is
// wasted on a value already ruled out.  The model itself learns exactly
// the same bits as plain v3, so any gain comes from the pointer decision.
#ifndef ESH
#define ESH 12
#endif
#ifndef EW0
#define EW0 65536
#endif
#ifndef EXCL
#define EXCL 1
#endif
#ifndef EAPM
#define EAPM 0
#endif

template <int TWOMIX>
static inline int predict(Tables *t, uint32_t *const *base, uint32_t c0, uint32_t nib, int mexp, int mmv,
                          int ms, int letter, int last, int sel2, int32_t *x) {
    for (int i = 0; i < NH; i++) x[i] = STI[(base[i][nib] & 0xFFFF) >> 4];
    x[NH] = STI[(t->T0[c0] & 0xFFFF) >> 4];
    if (mexp >= 0) { int s = STI[mmv >> 4]; x[NM] = mexp ? s : -s; } else x[NM] = 0;
    x[NM + 1] = 64;
    int32_t *ws = t->w + (size_t)(c0 + 256 * ms + 1024 * letter) * NINP;
    int64_t dot = 0;
    for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * x[i];
    int s;
    if (TWOMIX) {
        int32_t *ws2 = t->w2 + (size_t)(c0 + 256 * sel2) * NINP;
        int64_t dot2 = 0;
        for (int i = 0; i < NIN; i++) dot2 += (int64_t)ws2[i] * x[i];
        int s1 = (int)(dot >> 16), s2 = (int)(dot2 >> 16);
        s1 = s1 > 2047 ? 2047 : (s1 < -2047 ? -2047 : s1);
        s2 = s2 > 2047 ? 2047 : (s2 < -2047 ? -2047 : s2);
        s = (s1 + s2) >> 1;
    } else s = (int)(dot >> 16);
    s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
    int pm = SQI[s + 2048];
    int arow_i = (int)(c0 + 256 * (uint32_t)last);
    const uint16_t *row = t->apm_ok[arow_i] ? t->apm + (size_t)arow_i * 33 : t->row0;
    int apos = s + 2048, j = apos >> 7, wt = apos & 127;
    int pa = (row[j] * (128 - wt) + row[j + 1] * wt) >> 11;
    int p = (pm * (256 - APMW) + pa * APMW) >> 8;
    return p < 1 ? 1 : (p > 4095 ? 4095 : p);
}

template <int DEC, int TWOMIX>
static void run(Tables *t, uint8_t *buf, size_t n, Coder *cd) {
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
    int32_t xd[NINP];
    for (int i = 0; i < NINP; i++) x[i] = 0;
    int last = 0, last2 = 0;
    uint32_t EP[32 * 4];
    int32_t EW[32 * 4][3];
    uint16_t EA[32 * 4][33];
    for (int i = 0; i < 128; i++) {
        EP[i] = 0x8000; EW[i][0] = EW0; EW[i][1] = 0; EW[i][2] = 0;
        for (int j = 0; j < 33; j++) { int xx = j * 128 - 2048; if (xx > 2047) xx = 2047; EA[i][j] = (uint16_t)(SQI[xx + 2048] * 16); }
    }
    int ehist = 0;
    ctx_hashes(h, buf, 0, 0, 0);
    for (size_t pos = 0; pos < n; pos++) {
        int c = DEC ? 0 : buf[pos];
        int mbyte = (mlen > 0 && (size_t)mptr < pos) ? buf[mptr] : -1;   // byte the match expects
        int mk = mlen < 63 ? (int)mlen : 63;
        int ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        int letter = ((last | 32) >= 'a' && (last | 32) <= 'z') ? 1 : 0;
        int sel2 = cls(last) * 5 + cls(last2);
        uint32_t c0 = 1, nib = 1;
        int code = 1;     // 0 = this byte came from the pointer (the trit was 2)
        int xb = -1;      // byte ruled out by a "no" answer
        uint32_t R[8];    // chance (16-bit) of finishing mbyte from below each bit
        {
            uint32_t xs[NH];
            for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], 1); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
            for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
        }
        if (PTR && mbyte >= 0 && mlen >= PMINL) {
            // dry pass: the model's chance of mbyte, bit by bit, no learning
            uint32_t *bB[NH];
            {
                uint32_t xs[NH], cc = 16 | (uint32_t)(mbyte >> 4);
                for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], cc); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                for (int i = 0; i < NH; i++) bB[i] = bucket_find(t->T[i], xs[i], bits);
            }
            int q[8];
            uint64_t P = (uint64_t)1 << 30;
            for (int bit = 7; bit >= 0; bit--) {
                uint32_t cc = (uint32_t)((mbyte | 256) >> (bit + 1));
                uint32_t nb2 = bit >= 4 ? cc : (uint32_t)(((mbyte & 15) | 16) >> (bit + 1));
                int b = (mbyte >> bit) & 1;
                int p = predict<TWOMIX>(t, bit >= 4 ? base : bB, cc, nb2, b, mm[mk], ms, letter, last, sel2, xd);
                q[bit] = b ? p : 4096 - p;
                P = (P * (uint64_t)q[bit]) >> 12;
            }
            R[0] = 65536;
            for (int k = 1; k < 8; k++) R[k] = (uint32_t)(((uint64_t)R[k - 1] * (uint64_t)q[k - 1]) >> 12);
            int P12 = (int)(P >> 18);
            P12 = P12 < 1 ? 1 : (P12 > 4095 ? 4095 : P12);
            int ectx = (mlen < 31 ? (int)mlen : 31) * 4 + ehist;
            uint32_t ee = EP[ectx];
            int pe0 = (int)(ee & 0xFFFF) >> 4;
            pe0 = pe0 < 1 ? 1 : (pe0 > 4095 ? 4095 : pe0);
            int xe[3] = {STI[P12], STI[pe0], 256};
            int64_t ed = (int64_t)EW[ectx][0] * xe[0] + (int64_t)EW[ectx][1] * xe[1] + (int64_t)EW[ectx][2] * xe[2];
            int es = (int)(ed >> 16);
            es = es > 2047 ? 2047 : (es < -2047 ? -2047 : es);
            int pe = SQI[es + 2048];
            int ej = 0, ewt = 0;
            if (EAPM) {
                int apos = es + 2048; ej = apos >> 7; ewt = apos & 127;
                int pa = (EA[ectx][ej] * (128 - ewt) + EA[ectx][ej + 1] * ewt) >> 11;
                pe = (pe + pa * 3) >> 2;
            }
            pe = pe < 1 ? 1 : (pe > 4095 ? 4095 : pe);
            int e;
            if (DEC) e = dec_bit(cd, (uint64_t)pe);
            else { e = c == mbyte; enc_bit(cd, e, (uint64_t)pe); }
            int err = (e << 12) - SQI[es + 2048];
            for (int i = 0; i < 3; i++) EW[ectx][i] += (xe[i] * err) >> ESH;
            if (EAPM) {
                int t16 = e ? 65535 : 0;
                EA[ectx][ej] = (uint16_t)(EA[ectx][ej] + (((t16 - EA[ectx][ej]) * (128 - ewt)) >> 7));
                EA[ectx][ej + 1] = (uint16_t)(EA[ectx][ej + 1] + (((t16 - EA[ectx][ej + 1]) * ewt) >> 7));
            }
            int pp = (int)(ee & 0xFFFF), nn = (int)(ee >> 16);
            pp += (((e ? 65535 : 0) - pp) * RT15[nn]) >> 15;
            nn = nn < PLIM ? nn + 1 : nn;
            EP[ectx] = (uint32_t)pp | ((uint32_t)nn << 16);
            ehist = ((ehist << 1) | e) & 3;
            npt++; if (e) nhit++;
            if (e) { c = mbyte; code = 0; }
            else if (EXCL) xb = mbyte;
        }
        for (int bit = 7; bit >= 0; bit--) {
            if (bit == 3) {   // second nibble: new blocks, chosen by the first nibble
                nib = 1;
                uint32_t xs[NH];
                for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], c0); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
            }
            for (int i = 0; i < NH; i++) slot[i] = base[i] + nib;
            slot[NH] = t->T0 + c0;
            for (int i = 0; i < NM; i++) x[i] = STI[(*slot[i] & 0xFFFF) >> 4];
            int mexp = -1;
            if (mbyte >= 0) {
                if ((uint32_t)((mbyte | 256) >> (bit + 1)) == c0) mexp = (mbyte >> bit) & 1;
                else mbyte = -1;
            }
            if (mexp >= 0) { int s = STI[mm[mk] >> 4]; x[NM] = mexp ? s : -s; } else x[NM] = 0;
            x[NM + 1] = 64;
            int32_t *ws = t->w + (size_t)(c0 + 256 * ms + 1024 * letter) * NINP;
            int64_t dot = 0;
            for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * x[i];
            int s, pm1 = 0, pm2 = 0;
            int32_t *ws2 = NULL;
            if (TWOMIX) {
                ws2 = t->w2 + (size_t)(c0 + 256 * sel2) * NINP;
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
            // refinement stage, indexed by partial byte and previous byte
            int arow_i = (int)(c0 + 256 * (uint32_t)last);
            uint16_t *row = t->apm + (size_t)arow_i * 33;
            if (!t->apm_ok[arow_i]) { memcpy(row, t->row0, sizeof(t->row0)); t->apm_ok[arow_i] = 1; }
            if (bit > 0) prefetch(t->apm + (size_t)(2 * c0 + 256 * (uint32_t)last) * 33);
            int apos = s + 2048, j = apos >> 7, wt = apos & 127;
            int pa = (row[j] * (128 - wt) + row[j + 1] * wt) >> 11;
            int p = (pm * (256 - APMW) + pa * APMW) >> 8;
            p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
            int y;
            if (!code) y = (c >> bit) & 1;
            else {
                int forced = -1;
                if (xb >= 0) {
                    if ((uint32_t)((xb | 256) >> (bit + 1)) == c0) {   // still on the ruled-out path
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
                    } else xb = -1;
                }
                if (forced >= 0) y = forced;
                else if (DEC) y = dec_bit(cd, (uint64_t)p);
                else { y = (c >> bit) & 1; enc_bit(cd, y, (uint64_t)p); }
            }
            // learn
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

