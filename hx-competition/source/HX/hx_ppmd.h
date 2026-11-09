// hx_ppmd.h - PPMd route for HX Complete 14 (PPMd var.I / var.H via Igor Pavlov's public-domain C code).
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See HX/LICENSE.txt.
// Body layout: [variant 1 byte: 7 or 8][order 1][mem MB 2 (LE)][restore 1] + range-coded symbols. Length is known to the caller.
#pragma once
extern "C" {
#include "Ppmd7.h"
#include "Ppmd8.h"
}
#include <vector>
#include <cstdint>
#include <cstdlib>

namespace hxppmd {
static void *pa_alloc(ISzAllocPtr, size_t s) { return malloc(s); }
static void pa_free(ISzAllocPtr, void *a) { free(a); }
static const ISzAlloc g_alloc = {pa_alloc, pa_free};

struct OutV { IByteOut vt; std::vector<uint8_t> *v; };
static void outv_write(IByteOutPtr pp, Byte b) { OutV *o = (OutV *)(void *)pp; o->v->push_back(b); }
struct InV { IByteIn vt; const uint8_t *p, *e; };
static Byte inv_read(IByteInPtr pp) { InV *i = (InV *)(void *)pp; return i->p < i->e ? *i->p++ : 0; }

// encode; returns body (with 5-byte parameter header) or empty on failure
inline std::vector<uint8_t> enc(const uint8_t *in, size_t n, int variant, unsigned order, unsigned memMB, unsigned restore = 0) {
    std::vector<uint8_t> out; out.reserve(n / 3 + 64);
    out.push_back((uint8_t)variant); out.push_back((uint8_t)order); out.push_back(memMB & 255); out.push_back(memMB >> 8); out.push_back((uint8_t)restore);
    OutV o; o.vt.Write = outv_write; o.v = &out;
    if (variant == 8) {
        CPpmd8 p; Ppmd8_Construct(&p);
        if (!Ppmd8_Alloc(&p, memMB << 20, &g_alloc)) return {};
        p.Stream.Out = &o.vt; Ppmd8_Init_RangeEnc(&p); Ppmd8_Init(&p, order, restore);
        for (size_t i = 0; i < n; i++) Ppmd8_EncodeSymbol(&p, in[i]);
        Ppmd8_Flush_RangeEnc(&p); Ppmd8_Free(&p, &g_alloc);
    } else {
        CPpmd7 p; Ppmd7_Construct(&p);
        if (!Ppmd7_Alloc(&p, memMB << 20, &g_alloc)) return {};
        p.rc.enc.Stream = &o.vt; Ppmd7z_Init_RangeEnc(&p); Ppmd7_Init(&p, order);
        Ppmd7z_EncodeSymbols(&p, in, in + n);
        Ppmd7z_Flush_RangeEnc(&p); Ppmd7_Free(&p, &g_alloc);
    }
    return out;
}

inline bool dec(const uint8_t *body, size_t m, uint8_t *out, size_t n) {
    if (m < 5) return false;
    int variant = body[0]; unsigned order = body[1], memMB = body[2] | (body[3] << 8), restore = body[4];
    InV iv; iv.vt.Read = inv_read; iv.p = body + 5; iv.e = body + m;
    if (variant == 8) {
        CPpmd8 p; Ppmd8_Construct(&p);
        if (!Ppmd8_Alloc(&p, memMB << 20, &g_alloc)) return false;
        p.Stream.In = &iv.vt; Ppmd8_Init(&p, order, restore);
        if (!Ppmd8_Init_RangeDec(&p)) { Ppmd8_Free(&p, &g_alloc); return false; }
        for (size_t i = 0; i < n; i++) { int s = Ppmd8_DecodeSymbol(&p); if (s < 0) { Ppmd8_Free(&p, &g_alloc); return false; } out[i] = (uint8_t)s; }
        Ppmd8_Free(&p, &g_alloc); return true;
    } else if (variant == 7) {
        CPpmd7 p; Ppmd7_Construct(&p);
        if (!Ppmd7_Alloc(&p, memMB << 20, &g_alloc)) return false;
        p.rc.dec.Stream = &iv.vt; Ppmd7_Init(&p, order);
        if (!Ppmd7z_RangeDec_Init(&p.rc.dec)) { Ppmd7_Free(&p, &g_alloc); return false; }
        for (size_t i = 0; i < n; i++) { int s = Ppmd7z_DecodeSymbol(&p); if (s < 0) { Ppmd7_Free(&p, &g_alloc); return false; } out[i] = (uint8_t)s; }
        Ppmd7_Free(&p, &g_alloc); return true;
    }
    return false;
}
} // namespace hxppmd
