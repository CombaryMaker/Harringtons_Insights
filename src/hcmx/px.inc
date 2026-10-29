// ---- Rebuilt 2026-10-27: hcm-trit v2 (ternary pointer, "2 = pointer") and HCM-Bin v1
// (pointer + 4 extra record contexts), on top of the v3.3 engine. Michael Harrington's designs.
//
// Pointer: before each byte, a candidate byte is guessed (the byte the long match expects,
// otherwise the last byte seen after the same 4-byte context). The model's own chance of that
// byte is worked out bit by bit (the walk). One yes/no flag "is it the candidate?" is coded,
// its chance refined by a small mixer (model chance, an escape cell keyed by candidate type,
// match length and the last two flag outcomes, and a cell keyed by the previous 2 bytes and
// the candidate). On yes the byte costs only the flag; the model still learns its 8 bits
// from the walk. On no the byte is coded with the candidate excluded: its share is removed
// from each bit's chance while the bits still follow the candidate's path (the walk's
// predictions are reused there), and the last bit is forced.
// HCM-Bin: for binary files with a detected record length, 4 record contexts are added
// (row above + above-right, row above + last byte, rows above 1-2, rows above 1-3) and the
// word and order-8 contexts are kept.

enum { NHX = NH + 4, NMXX = NHX + 1, NIN16 = 16 };
enum { FLAG_PTR = 64, FLAG_XREC = 128 };
#ifndef FLR
#define FLR 12
#endif
#ifndef ER
#define ER 5
#endif
#ifndef FR
#define FR 4
#endif
static int g_noptr = 0;   // test switch: pointer off (record contexts alone)

static inline void train16(int32_t *w, const int32_t *x, int err) {
#if HCM_SSE2
    __m128i ev = _mm_set1_epi32(err & 0xFFFF), rv = _mm_set1_epi32(1 << (LRSHIFT - 1));
    for (int i = 0; i < 16; i += 4) {
        __m128i xv = _mm_load_si128((const __m128i *)(x + i));
        __m128i pv = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(xv, ev), rv), LRSHIFT);
        __m128i wv = _mm_load_si128((const __m128i *)(w + i));
        _mm_store_si128((__m128i *)(w + i), _mm_add_epi32(wv, pv));
    }
#else
    for (int i = 0; i < 16; i++) w[i] += (x[i] * err + (1 << (LRSHIFT - 1))) >> LRSHIFT;
#endif
}

struct BitState {
    uint32_t *slot[NMXX];
#if defined(_MSC_VER)
    __declspec(align(16)) int32_t x[16];
#else
    int32_t x[16] __attribute__((aligned(16)));
#endif
    int32_t *ws, *ws2;
    int pm, pm1, pm2, p, j, wt, mexp;
    uint16_t *row;
};

struct PxTables {
    uint32_t *T[NHX]; void *Traw[NHX];
    int32_t *w; void *wraw; int32_t *w2; void *w2raw;
    uint16_t *E; uint16_t *F; int32_t *fw; uint16_t *c4;
};

template <int DEC, int TWOMIX, int XREC>
static void run_px(Tables *t, uint8_t *buf, size_t n, Coder *cd, int isbin) {
    const int APMW = APMW_V33, MMR = MMR_V33;
    const int nh = XREC ? NHX : NH, nm = nh + 1, nin = nm + 2;
    static RecState rs; rec_init(&rs); for (int c = 0; c < 256; c++) CLS[c] = (uint8_t)cls(c);
    const int bits = t->bits;
    PxTables px; memset(&px, 0, sizeof(px));
    for (int i = 0; i < NH; i++) px.T[i] = t->T[i];
    for (int i = NH; i < nh; i++) px.T[i] = (uint32_t *)alloc64(((size_t)16 << bits) * 4, &px.Traw[i]);
    const int wsets = 8192;
    px.w = (int32_t *)alloc64(sizeof(int32_t) * 16 * wsets, &px.wraw);
    for (int r = 0; r < wsets; r++) for (int i = 0; i < 16; i++) px.w[(size_t)r * 16 + i] = i < nin ? WINIT : 0;
    if (TWOMIX) {
        px.w2 = (int32_t *)alloc64(sizeof(int32_t) * 16 * 256 * 25, &px.w2raw);
        for (int r = 0; r < 256 * 25; r++) for (int i = 0; i < 16; i++) px.w2[(size_t)r * 16 + i] = i < nin ? WINIT : 0;
    }
    px.E = (uint16_t *)malloc(2 * 64); for (int i = 0; i < 64; i++) px.E[i] = 32768;
    px.F = (uint16_t *)malloc(2 * 65536); for (int i = 0; i < 65536; i++) px.F[i] = 32768;
    px.fw = (int32_t *)malloc(4 * 4 * 64); for (int i = 0; i < 64; i++) { px.fw[i*4] = 65536; px.fw[i*4+1] = 16384; px.fw[i*4+2] = 16384; px.fw[i*4+3] = 0; }
    const int c4bits = bits + 4 > 22 ? 22 : bits + 4;
    px.c4 = (uint16_t *)calloc((size_t)1 << c4bits, 2);
    int wl = 0; int64_t lnl = -1; uint32_t hroll = 0;
    uint32_t h[NHX], *base[NHX];
    uint32_t word = 0, pword = 0;
    int64_t mptr = 0, mlen = 0;
    uint16_t mm[64]; for (int i = 0; i < 64; i++) mm[i] = 49152;
    int last = 0, last2 = 0, fhist = 0;
    BitState bs[8], cur;
    ctx_hashes(h, buf, 0, 0, 0);
    for (int i = NH; i < nh; i++) h[i] = 0x1234567u * (uint32_t)(i + 1);

    // one bit: prediction into S (uses base[], mbyte, c0, nib)
    auto predict = [&](BitState &S, uint32_t c0, uint32_t nib, int bit, int &mbyte, int mk, int ms, int letter, int sel2) {
        for (int i = 0; i < nh; i++) S.slot[i] = base[i] + nib;
        S.slot[nh] = t->T0 + c0;
        for (int i = 0; i < nm; i++) S.x[i] = STI[(*S.slot[i] & 0xFFFF) >> 4];
        S.mexp = -1;
        if (mbyte >= 0) {
            if ((uint32_t)((mbyte | 256) >> (bit + 1)) == c0) S.mexp = (mbyte >> bit) & 1;
            else mbyte = -1;
        }
        if (S.mexp >= 0) { int s = STI[mm[mk] >> 4]; S.x[nm] = S.mexp ? s : -s; } else S.x[nm] = 0;
        S.x[nm + 1] = 64;
        for (int i = nin; i < 16; i++) S.x[i] = 0;
        S.ws = px.w + (size_t)(c0 + 256 * ms + 1024 * letter) * 16;
        int64_t dot = 0;
        for (int i = 0; i < nin; i++) dot += (int64_t)S.ws[i] * S.x[i];
        int s;
        if (TWOMIX) {
            S.ws2 = px.w2 + (size_t)(c0 + 256 * sel2) * 16;
            int64_t dot2 = 0;
            for (int i = 0; i < nin; i++) dot2 += (int64_t)S.ws2[i] * S.x[i];
            int s1 = (int)(dot >> 16), s2 = (int)(dot2 >> 16);
            s1 = s1 > 2047 ? 2047 : (s1 < -2047 ? -2047 : s1);
            s2 = s2 > 2047 ? 2047 : (s2 < -2047 ? -2047 : s2);
            S.pm1 = SQI[s1 + 2048]; S.pm2 = SQI[s2 + 2048];
            s = (s1 + s2) >> 1;
        } else s = (int)(dot >> 16);
        s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
        S.pm = SQI[s + 2048];
        int arow_i = (int)(c0 + 256 * (uint32_t)last);
        S.row = t->apm + (size_t)arow_i * 33;
        if (!t->apm_ok[arow_i]) { memcpy(S.row, t->row0, sizeof(t->row0)); t->apm_ok[arow_i] = 1; }
        int apos = s + 2048; S.j = apos >> 7; S.wt = apos & 127;
        int pa = (S.row[S.j] * (128 - S.wt) + S.row[S.j + 1] * S.wt) >> 11;
        int p = (S.pm * (256 - APMW) + pa * APMW) >> 8;
        S.p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
    };
    auto learn = [&](BitState &S, int y, int mk) {
        if (TWOMIX) { train16(S.ws2, S.x, (y << 12) - S.pm2); train16(S.ws, S.x, (y << 12) - S.pm1); }
        else train16(S.ws, S.x, (y << 12) - S.pm);
        int tgt = y ? 65535 : 0;
        for (int i = 0; i < nm; i++) {
            uint32_t e = *S.slot[i];
            int pp = (int)(e & 0xFFFF), nn = (int)(e >> 16);
            pp += ((tgt - pp) * RT15[nn]) >> 15;
            nn = i == 0 ? NEXT60[nn] : NEXT255[nn];
            *S.slot[i] = (uint32_t)pp | ((uint32_t)nn << 16);
        }
        if (S.mexp >= 0) { int v = mm[mk]; int hit = y == S.mexp ? 65535 : 0; mm[mk] = (uint16_t)(v + ((hit - v) >> MMR)); }
        int t16 = y ? 65535 : 0;
        S.row[S.j] = (uint16_t)(S.row[S.j] + (((t16 - S.row[S.j]) * (128 - S.wt)) >> APMRATE));
        S.row[S.j + 1] = (uint16_t)(S.row[S.j + 1] + (((t16 - S.row[S.j + 1]) * S.wt) >> APMRATE));
    };
    auto find_base = [&](uint32_t c0) {
        uint32_t xs[NHX];
        for (int i = 0; i < nh; i++) { xs[i] = bucket_mix(h[i], c0); prefetch(bucket_addr(px.T[i], xs[i], bits)); }
        for (int i = 0; i < nh; i++) base[i] = bucket_find(px.T[i], xs[i], bits);
    };
    auto codebit = [&](int y, int p) -> int {   // p = chance of 1 (1..4095)
        if (DEC) return dec_bit(cd, (uint64_t)p);
        enc_bit(cd, y, (uint64_t)p); return y;
    };

    for (size_t pos = 0; pos < n; pos++) {
        int c = DEC ? 0 : buf[pos];
        int mbyte0 = (mlen > 0 && (size_t)mptr < pos) ? buf[mptr] : -1;
        int mk = mlen < 63 ? (int)mlen : 63;
        int ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        int letter;
        { int cl = CLS[last]; letter = wl == 0 ? (cl == 3 ? 0 : cl == 2 ? 1 : cl == 4 ? 2 : 3) : wl == 1 ? 4 : wl == 2 ? 5 : wl < 5 ? 6 : 7; }
        int sel2 = cls(last) * 5 + cls(last2);
        // candidate
        uint32_t c4h = 0; int cand = -1, ctype = 0;
        if (pos >= 4) {
            c4h = hfin(buf[pos-1] | (buf[pos-2] << 8) | (buf[pos-3] << 16) | ((uint32_t)buf[pos-4] << 24), 0xC4) >> (32 - c4bits);
        }
        if (!g_noptr && mbyte0 >= 0) { cand = mbyte0; ctype = 0; }
        int ccnt = 0;
        if (cand < 0 && !g_noptr && pos >= 4 && (px.c4[c4h] & 0x100)) { cand = px.c4[c4h] & 255; ctype = 1; ccnt = px.c4[c4h] >> 9; }
        find_base(1);
        uint32_t c0 = 1, nib = 1;
        int mbyte = mbyte0;
        int byte;
        if (cand >= 0) {
            // walk the candidate's path without learning
            uint32_t *base1[NHX]; for (int i = 0; i < nh; i++) base1[i] = base[i];
            int mbw = mbyte0; uint32_t wc0 = 1, wnib = 1; uint32_t pk[8];
            for (int bit = 7; bit >= 0; bit--) {
                if (bit == 3) { wnib = 1; find_base(wc0); }
                predict(bs[bit], wc0, wnib, bit, mbw, mk, ms, letter, sel2);
                int b = (cand >> bit) & 1;
                pk[bit] = (uint32_t)(b ? bs[bit].p : 4096 - bs[bit].p);
                wc0 = (wc0 << 1) | (uint32_t)b; wnib = (wnib << 1) | (uint32_t)b;
            }
            uint32_t *base2[NHX]; for (int i = 0; i < nh; i++) base2[i] = base[i];
            uint32_t S[8];   // S[k] = chance (2^24 scale) the remaining bits k..0 all follow the candidate
            S[0] = pk[0] << 12;
            for (int k = 1; k < 8; k++) S[k] = (uint32_t)(((uint64_t)pk[k] * S[k - 1]) >> 12);
            // flag
            int pc = (int)(S[7] >> 12); pc = pc < 1 ? 1 : (pc > 4095 ? 4095 : pc);
            int lb = mlen < 8 ? 0 : mlen < 12 ? 1 : mlen < 16 ? 2 : mlen < 24 ? 3 : mlen < 32 ? 4 : mlen < 64 ? 5 : mlen < 256 ? 6 : 7;
            int ecell = ctype ? 32 + (ccnt < 7 ? ccnt : 7) * 4 + (fhist & 3) : lb * 4 + (fhist & 3);
            uint32_t fcell = hfin(last | (last2 << 8) | ((uint32_t)cand << 16), 0xF1) & 0xFFFF;
            int32_t xf[4] = { STI[pc], STI[px.E[ecell] >> 4], STI[px.F[fcell] >> 4], 256 };
            int32_t *fw = px.fw + ecell * 4;
            int64_t fd = 0; for (int i = 0; i < 4; i++) fd += (int64_t)fw[i] * xf[i];
            int fs = (int)(fd >> 16); fs = fs > 2047 ? 2047 : (fs < -2047 ? -2047 : fs);
            int pf = SQI[fs + 2048];
            int yf = codebit(DEC ? 0 : (c == cand), pf);
            int ferr = (yf << 12) - pf;
            for (int i = 0; i < 4; i++) fw[i] += (xf[i] * ferr + (1 << (FLR - 1))) >> FLR;
            int tf = yf ? 65535 : 0;
            px.E[ecell] = (uint16_t)(px.E[ecell] + ((tf - px.E[ecell]) >> ER));
            px.F[fcell] = (uint16_t)(px.F[fcell] + ((tf - px.F[fcell]) >> FR));
            fhist = ((fhist << 1) | yf) & 3;
            if (yf) {
                // byte = candidate: learn the 8 bits from the walk
                for (int bit = 7; bit >= 0; bit--) learn(bs[bit], (cand >> bit) & 1, mk);
                byte = cand;
            } else {
                // code with the candidate excluded while the path follows it
                int follow = 1;
                for (int bit = 7; bit >= 0; bit--) {
                    int b = (cand >> bit) & 1, y;
                    if (follow) {
                        BitState &B = bs[bit];
                        if (bit == 0) y = 1 - b;                  // forced: candidate excluded
                        else {
                            uint32_t pb24 = (uint32_t)(b ? B.p : 4096 - B.p) << 12;
                            uint32_t num = pb24 - S[bit], den = (1u << 24) - S[bit];
                            int pbx = (int)(((uint64_t)num * 4096) / den);
                            pbx = pbx < 1 ? 1 : (pbx > 4095 ? 4095 : pbx);
                            int p1 = b ? pbx : 4096 - pbx; p1 = p1 < 1 ? 1 : (p1 > 4095 ? 4095 : p1);
                            y = codebit(DEC ? 0 : (c >> bit) & 1, p1);
                        }
                        learn(B, y, mk);
                        if (y != b) {
                            follow = 0; mbyte = -1;
                            for (int i = 0; i < nh; i++) base[i] = bit >= 4 ? base1[i] : base2[i];
                        }
                    } else {
                        if (bit == 3) find_base(c0 << 0);          // only reached when the first nibble differed
                        predict(cur, c0, nib, bit, mbyte, mk, ms, letter, sel2);
                        y = codebit(DEC ? 0 : (c >> bit) & 1, cur.p);
                        learn(cur, y, mk);
                    }
                    c0 = (c0 << 1) | (uint32_t)y; nib = (nib << 1) | (uint32_t)y;
                    if (bit == 4) nib = 1;
                }
                byte = (int)(c0 & 255);
            }
        } else {
            for (int bit = 7; bit >= 0; bit--) {
                if (bit == 3) { nib = 1; find_base(c0); }
                predict(cur, c0, nib, bit, mbyte, mk, ms, letter, sel2);
                int y = codebit(DEC ? 0 : (c >> bit) & 1, cur.p);
                learn(cur, y, mk);
                c0 = (c0 << 1) | (uint32_t)y; nib = (nib << 1) | (uint32_t)y;
            }
            byte = (int)(c0 & 255);
        }
        buf[pos] = (uint8_t)byte;
        if (pos >= 4) {
            uint16_t e = px.c4[c4h];
            if ((e & 0x100) && (e & 255) == byte) { int k = e >> 9; if (k < 127) k++; px.c4[c4h] = (uint16_t)((k << 9) | 0x100 | byte); }
            else px.c4[c4h] = (uint16_t)(0x100 | byte);
        }
        size_t nb = pos + 1;
        if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)) { word = (word * 0x2C9277B5u) ^ (uint32_t)(byte | 32); wl++; }
        else { if (word) pword = word; word = 0; wl = 0; }
        if (mlen > 0 && (size_t)mptr < nb - 1 && buf[mptr] == byte) { mptr++; mlen++; }
        else if (g_v34 && mlen >= 24 && (size_t)mptr < nb - 1) { mptr++; mlen = 16; }   // v3.4 match recovery
        else mlen = 0;
        if (nb >= 6) {
            uint32_t hh;
            if (nb >= 7) {
                hroll = (uint32_t)((((uint64_t)((hroll - buf[nb - 7] + 0x100000u) & 0xFFFFF) * KINV20) + (uint64_t)buf[nb - 1] * K5_20) & 0xFFFFF);
                hh = hroll;
            } else {
                hh = 0;
                for (int j2 = 1; j2 <= 6; j2++) hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + buf[nb - j2]) & 0xFFFFF);
                hroll = hh;
            }
            hh &= t->mmask;
            if (mlen == 0) {
                int64_t cnd = t->mtab[hh];
                if (cnd > 0) {
                    int L = 0;
                    while (L < 32 && cnd - 1 - L >= 0 && buf[cnd - 1 - L] == buf[nb - 1 - L]) L++;
                    if (L >= 6) { mptr = cnd; mlen = L; }
                }
            }
            t->mtab[hh] = (int32_t)nb;
        }
        last2 = last; last = byte;
        ctx_hashes(h, buf, nb, word, pword);
        if (isbin) {
            rec_update(&rs, buf, nb);
            if (XREC) {
                size_t R = (size_t)rs.R;
                if (R) {
                    uint32_t up = nb >= R ? buf[nb - R] : 256, upr = (nb + 1 >= R && R > 1) ? buf[nb - R + 1] : 256;
                    uint32_t up2 = nb >= 2 * R ? buf[nb - 2 * R] : 256, up3 = nb >= 3 * R ? buf[nb - 3 * R] : 256, c1 = buf[nb - 1];
                    uint32_t RR = (uint32_t)R * 0x9E3779B1u;
                    h[NH]     = hfin(RR + up * 0x1000193u + upr, 0x11);
                    h[NH + 1] = hfin(RR + up * 0x1000193u + c1 * 0x3B9AC9FBu, 0x22);
                    h[NH + 2] = hfin(RR + up * 0x1000193u + up2 * 0x3B9AC9FBu, 0x33);
                    h[NH + 3] = hfin(RR + up * 0x1000193u + up2 * 0x3B9AC9FBu + up3 * 0x6F4F2A35u, 0xAA);
                } else for (int i = NH; i < nh; i++) h[i] = 0x1234567u * (uint32_t)(i + 1);
            } else rec_swap(h, buf, nb, &rs);
            if (g_v34) h[4] = num_ctx(buf, nb, rs.R);   // v3.4 numeric context
        } else {
            if (byte == '\n') lnl = (int64_t)nb - 1;
            uint32_t col = (uint32_t)((int64_t)nb - 1 - lnl); col = col < 255 ? col : 255;
            h[4] = hfin((uint32_t)wl * 0x3B9AC9FBu + (uint32_t)CLS[byte] * 0x6F4F2A35u + col * 0x2545F491u, 0x5A);
        }
    }
    for (int i = NH; i < nh; i++) free(px.Traw[i]);
    free(px.wraw); free(px.w2raw); free(px.E); free(px.F); free(px.fw); free(px.c4);
}
