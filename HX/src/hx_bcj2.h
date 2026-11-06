// hx_bcj2.h - BCJ2 x86 branch split (Igor Pavlov's public-domain C code) for the HX Complete 14 LZMA route.
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See HX/LICENSE.txt.
#pragma once
extern "C" {
#include "Bcj2.h"
}
#include <vector>
#include <cstdint>
#include <cstring>

namespace hxbcj2 {
// split src into 4 streams (main, call, jump, rc). Returns false on failure.
inline bool enc(const uint8_t *src, size_t n, std::vector<uint8_t> s[4]) {
    for (int i = 0; i < 4; i++) s[i].assign(n + (1 << 16), 0);
    CBcj2Enc e; Bcj2Enc_Init(&e);
    for (int i = 0; i < 4; i++) { e.bufs[i] = s[i].data(); e.lims[i] = s[i].data() + s[i].size(); }
    e.src = src; e.srcLim = src + n; e.finishMode = BCJ2_ENC_FINISH_MODE_END_STREAM;
    Bcj2Enc_SET_FileSize(&e, n); e.relatLimit = BCJ2_ENC_RELAT_LIMIT_DEFAULT;
    for (int guard = 0; guard < 8; guard++) {
        Bcj2Enc_Encode(&e);
        if (e.state == BCJ2_ENC_STATE_FINISHED) break;
        if (e.state < BCJ2_NUM_STREAMS) return false;   // ran out of output space (should not happen)
    }
    if (e.state != BCJ2_ENC_STATE_FINISHED) return false;
    for (int i = 0; i < 4; i++) s[i].resize(e.bufs[i] - s[i].data());
    return true;
}
inline bool dec(const std::vector<uint8_t> s[4], uint8_t *out, size_t n) {
    CBcj2Dec d; Bcj2Dec_Init(&d);
    for (int i = 0; i < 4; i++) { d.bufs[i] = s[i].data(); d.lims[i] = s[i].data() + s[i].size(); }
    d.dest = out; d.destLim = out + n;
    if (Bcj2Dec_Decode(&d) != SZ_OK) return false;
    for (int guard = 0; guard < 4 && d.dest != d.destLim; guard++) if (Bcj2Dec_Decode(&d) != SZ_OK) return false;
    return d.dest == d.destLim && d.bufs[0] == d.lims[0];
}
} // namespace hxbcj2
