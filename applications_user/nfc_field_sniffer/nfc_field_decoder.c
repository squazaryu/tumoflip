/* GPL-3.0 passive demodulation adapted from ARF9b18419de2,
 * applications/system/nfc_sniffer/nfc_sniffer_hf.c; @thedoctorz.
 * No registers, allocation, RF transmission or key-recovery tools here. */
#include "nfc_field_decoder.h"
#include <string.h>
#define NFC_FIELD_TRANSITIONS 1536
#define NFC_FIELD_PAUSES      256
#define ETU100                2324
#define ETUSAMP               23
#define FRAME_GAP             81
struct sfr {
    uint16_t t, tend, bits;
    char dir;
    uint8_t n;
    uint32_t par;
    bool truncated;
    uint8_t b[24];
};
static struct {
    uint16_t transitions[NFC_FIELD_TRANSITIONS], pauses[NFC_FIELD_PAUSES];
    uint8_t sequence[160];
    struct sfr frames[NFC_FIELD_FRAME_CAP];
    NfcFieldTrace trace;
    uint32_t samples, base;
} nfc_field_memory;
static uint16_t* const s_tr = nfc_field_memory.transitions;
static uint16_t* const s_paus = nfc_field_memory.pauses;
static uint8_t* const s_seq = nfc_field_memory.sequence;
static struct sfr* const s_fr = nfc_field_memory.frames;
static int s_ntr, s_ongrid, s_offgrid, s_card_samp, s_nfr, s_tr_init, s_lvl, s_diag_maxrun, s_pos,
    s_last_bits;
static uint32_t s_diag_dense;
static bool s_decode_truncated;
static const uint8_t s_cap_shift = 4;
_Static_assert(sizeof(nfc_field_memory) < 16384, "NFC decoder workspace grew");
static int miller_decode(const uint16_t* ps, int np, uint8_t* out, uint32_t* par) {
    *par = 0;
    s_decode_truncated = false;
    uint8_t* seq = s_seq;
    __builtin_memset(seq, 0, 160);
    long base = (long)ps[0] * 100 - ETU100 / 4;
    int maxi = 0;
    for(int i = 0; i < np; i++) {
        long rel = (long)ps[i] * 100 - base;
        if(rel < 0) rel = 0;
        int idx = (int)(rel / ETU100);
        long within = rel - (long)idx * ETU100;
        long dZ = within - ETU100 / 4, dX = within - 3 * ETU100 / 4;
        if(dZ < 0) dZ = -dZ;
        if(dX < 0) dX = -dX;
        int half = (dX < dZ) ? 1 : 0;
        if((half ? dX : dZ) > ETU100 * 16 / 100) {
            s_offgrid++;
            continue;
        }
        s_ongrid++;
        if(idx >= 0 && idx < 160) {
            if(seq[idx] == 0) seq[idx] = half ? 2 : 1;
            if(idx > maxi) maxi = idx;
        } else
            s_decode_truncated = true;
        base = (long)ps[i] * 100 - (long)idx * ETU100 - (half ? 3 * ETU100 / 4 : ETU100 / 4);
    }
    int sr = 0, nb = 0, n = 0;
    for(int k = 1; k <= maxi && n < 32; k++) {
        int bit = (seq[k] == 2) ? 1 : 0;
        sr = (sr >> 1) | (bit << 8);
        nb++;
        if(nb == 9) {
            int v = sr & 0xFF;
            if((sr >> 8 & 1) == __builtin_parity(v)) *par |= (1u << n);
            out[n++] = (uint8_t)v;
            sr = 0;
            nb = 0;
        }
    }

    s_last_bits = n * 8;
    if(n == 0 && nb >= 6 && nb <= 7) {
        out[n++] = (uint8_t)((sr >> (9 - nb)) & ((1 << nb) - 1));
        s_last_bits = nb;
    }
    return n;
}
static int manch_decode(int s, int e, uint8_t* out, uint32_t* par) {
    *par = 0;
    s_decode_truncated = false;
    const uint16_t* tr = s_tr;
    int nt = s_ntr;

    int st = 0, hi = nt;
    while(st < hi) {
        int m = (st + hi) / 2;
        if(tr[m] < s)
            st = m + 1;
        else
            hi = m;
    }
    if(st >= nt) return 0;
    int t0 = tr[st];
    uint8_t bits[200];
    int nbits = 0;
    int p = st;

    int h1 = t0, mid = t0 + ETUSAMP / 2, end = t0 + ETUSAMP, frac = 0;
    for(int k = 0; k < 200; k++) {
        if(h1 > e + ETUSAMP) break;
        int c1 = 0;
        while(p < nt && tr[p] < mid) {
            p++;
            c1++;
        }
        int c2 = 0;
        while(p < nt && tr[p] < end) {
            p++;
            c2++;
        }
        if(c1 < 3 && c2 < 3) break;
        bits[nbits++] = (c1 >= 3) ? 1 : 0;
        int adv = ETU100 / 100;
        frac += ETU100 % 100;
        if(frac >= 100) {
            adv++;
            frac -= 100;
        }
        h1 += adv;
        mid += adv;
        end += adv;
    }
    if(nbits == 200 && h1 <= e + ETUSAMP) s_decode_truncated = true;
    int n = 0, i = 1;
    while(i + 8 <= nbits && n < 32) {
        int v = 0;
        for(int j = 0; j < 8; j++)
            v |= (bits[i + j] & 1) << j;
        if(i + 8 < nbits && (bits[i + 8] & 1) == __builtin_parity(v)) *par |= (1u << n);
        out[n++] = (uint8_t)v;
        i += 9;
    }

    s_last_bits = n * 8;
    if(n == 0 && nbits >= 5 && nbits <= 6) {
        int v = 0;
        for(int j = 1; j <= 4; j++)
            v |= (bits[j] & 1) << (j - 1);
        out[n++] = (uint8_t)(v & 0x0F);
        s_last_bits = 4;
    }
    return n;
}
static void add_frame(int t, int tend, char dir, const uint8_t* b, int n, uint32_t par) {
    if(n <= 0) return;
    if((unsigned)s_nfr >= NFC_FIELD_FRAME_CAP) {
        nfc_field_memory.trace.frames_dropped++;
        return;
    }
    // Value-based noise suppression would hide valid short NAK0 and diagnostic
    // all-zero/all-one payloads. Keep framed candidates; CRC remains unverified.
    struct sfr* f = &s_fr[s_nfr++];
    memset(f, 0, sizeof(*f));
    f->t = t;
    f->tend = tend;
    f->dir = dir;
    f->par = par;
    f->truncated = (unsigned)n > NFC_FIELD_FRAME_BYTES || s_decode_truncated;
    f->n = (unsigned)n > NFC_FIELD_FRAME_BYTES ? NFC_FIELD_FRAME_BYTES : (unsigned)n;
    f->bits = s_last_bits > f->n * 8 ? f->n * 8 : s_last_bits;
    memcpy(f->b, b, f->n);
}
static int sniff_decode(uint32_t got) {
    s_nfr = 0;
    s_ongrid = 0;
    s_offgrid = 0;
    s_card_samp = 0;
    if(s_ntr < 4 || got < 4) return 0;
    int init = s_tr_init;
    (void)got;

    uint16_t* paus = s_paus;
    int np = 0;
    int dense = 0, in_burst = 0, bs = 0, be = 0;
    for(int i = 1; i < s_ntr; i++) {
        int w = s_tr[i] - s_tr[i - 1];
        if(w <= 4) {
            s_diag_dense++;
            if(++dense == 5 && !in_burst) {
                in_burst = 1;
                bs = s_tr[i - 5];
            }
            if(dense > s_diag_maxrun) s_diag_maxrun = dense;
            if(in_burst) be = s_tr[i];
        } else {
            if(in_burst) {
                if(s_tr[i] - be > 4 * ETUSAMP) {
                    uint8_t by[32];
                    uint32_t par;
                    int nn = manch_decode(bs, be, by, &par);
                    add_frame(bs, be, 'C', by, nn, par);
                    s_card_samp += be - bs;
                    in_burst = 0;
                }
            } else if(w >= 5 && (init ^ (i & 1)) == 0 && np < NFC_FIELD_PAUSES) {
                paus[np++] = (uint16_t)((s_tr[i - 1] + s_tr[i]) / 2);
            }

            if(w > ETUSAMP) dense = 0;
        }
    }
    if(in_burst) {
        uint8_t by[32];
        uint32_t par;
        int nn = manch_decode(bs, be, by, &par);
        add_frame(bs, be, 'C', by, nn, par);
        s_card_samp += be - bs;
    }

    for(int fs = 0, i = 1; i <= np; i++)
        if(i == np || paus[i] - paus[i - 1] >= FRAME_GAP) {
            if(i - fs >= 4) {
                uint8_t by[32];
                uint32_t par;
                int nn = miller_decode(paus + fs, i - fs, by, &par);
                add_frame(paus[fs], paus[i - 1], 'R', by, nn, par);
            }
            fs = i;
        }

    for(int i = 0; i < s_nfr; i++)
        for(int j = i + 1; j < s_nfr; j++)
            if(s_fr[j].t < s_fr[i].t) {
                struct sfr t = s_fr[i];
                s_fr[i] = s_fr[j];
                s_fr[j] = t;
            }

    return s_nfr;
}
static void scan_span(const uint8_t* seg, int len, int* pos_io) {
    int lvl = s_lvl, ntr = s_ntr, tr_init = s_tr_init, pos = *pos_io;
    uint16_t* tr = s_tr;
    if(ntr == 0) pos = 0;
    int i = 0;

#define STEPB(b, idx)                                                            \
    do {                                                                         \
        int _b = (int)(b);                                                       \
        if(_b != lvl) {                                                          \
            if(ntr == 0) tr_init = lvl;                                          \
            if(ntr < NFC_FIELD_TRANSITIONS) tr[ntr++] = (uint16_t)(pos + (idx)); \
            lvl = _b;                                                            \
        }                                                                        \
    } while(0)
#define STEP(idx) STEPB((seg[idx] >> s_cap_shift) & 1, idx)

    while(i < len && ((uintptr_t)(seg + i) & 3u)) {
        STEP(i);
        i++;
    }

    for(; i + 4 <= len; i += 4) {
        uint32_t raw;
        memcpy(&raw, seg + i, sizeof(raw));
        uint32_t bits = (raw >> s_cap_shift) & 0x01010101u;
        uint32_t edg = (bits ^ ((bits << 8) | (uint32_t)lvl)) & 0x01010101u;
        if(edg) {
            do {
                int idx = __builtin_ctz(edg) >> 3;
                if(ntr == 0) tr_init = lvl;
                if(ntr < NFC_FIELD_TRANSITIONS) tr[ntr++] = (uint16_t)(pos + i + idx);
                edg &= edg - 1;
            } while(edg);
            lvl = (int)((bits >> 24) & 1u);
        }
    }
    for(; i < len; i++)
        STEP(i);
#undef STEP
#undef STEPB

    s_lvl = lvl;
    s_ntr = ntr;
    s_tr_init = tr_init;
    *pos_io = pos + len;
}
static bool region_valid(void) {
    for(int i = 0; i < s_nfr; i++)
        if(s_fr[i].dir == 'C' && s_fr[i].bits == 4 && !s_fr[i].par && !s_fr[i].truncated)
            return true;
    return (s_ongrid >= 4 && s_ongrid >= s_offgrid) || (s_card_samp >= 246);
}
static int find_cut(int start, int* cp, int* mgv, int* mgn, int* mgp) {
    const uint16_t* tr = s_tr;
    int nt = s_ntr;
    int cut_ne = 0, cut_pos = 0, mv = *mgv, mn = *mgn, mp = *mgp, prev = tr[start - 1];
    for(int i = start; i < nt; i++) {
        int cur = tr[i], gap = cur - prev;
        if(gap >= FRAME_GAP) {
            cut_ne = i;
            cut_pos = cur;
        }
        if(gap > mv) {
            mv = gap;
            mn = i;
            mp = cur;
        }
        prev = cur;
    }
    *cp = cut_pos;
    *mgv = mv;
    *mgn = mn;
    *mgp = mp;
    return cut_ne;
}
static void nfc_field_collect(uint32_t base, bool truncated) {
    for(int i = 0; i < s_nfr; i++) {
        if(nfc_field_memory.trace.count >= NFC_FIELD_FRAME_CAP) {
            nfc_field_memory.trace.frames_dropped++;
            continue;
        }
        const struct sfr* from = &s_fr[i];
        NfcFieldFrame* to = &nfc_field_memory.trace.frames[nfc_field_memory.trace.count++];
        to->start_us = (uint32_t)(((uint64_t)base + from->t) * NFC_FIELD_SAMPLE_CYCLES / 64);
        to->end_us = (uint32_t)(((uint64_t)base + from->tend) * NFC_FIELD_SAMPLE_CYCLES / 64);
        to->direction = from->dir;
        to->size = from->n;
        to->bits = from->bits;
        to->parity_errors = from->par;
        to->truncated = truncated || from->truncated;
        memcpy(to->data, from->b, from->n);
        to->crc_match = false;
        if(!to->truncated && !to->parity_errors && to->size >= 3 && to->bits == to->size * 8) {
            uint16_t crc = nfc_field_crc_a(to->data, to->size - 2);
            to->crc_match = to->data[to->size - 2] == (uint8_t)crc &&
                            to->data[to->size - 1] == (uint8_t)(crc >> 8);
        }
    }
}
void nfc_field_decoder_reset(bool initial_level) {
    memset(&nfc_field_memory, 0, sizeof(nfc_field_memory));
    s_ntr = s_nfr = s_pos = 0;
    s_lvl = initial_level;
    s_tr_init = s_lvl;
    s_ongrid = s_offgrid = s_card_samp = s_diag_maxrun = s_last_bits = 0;
    s_diag_dense = 0;
    s_decode_truncated = false;
}
size_t nfc_field_decoder_workspace_size(void) {
    return sizeof(nfc_field_memory);
}
const NfcFieldTrace* nfc_field_decoder_trace(void) {
    return &nfc_field_memory.trace;
}
void nfc_field_decoder_discontinuity(bool initial_level) {
    nfc_field_memory.trace.overruns++;
    s_ntr = s_nfr = s_pos = 0;
    s_lvl = initial_level;
    s_tr_init = s_lvl;
    nfc_field_memory.base = nfc_field_memory.samples;
}
bool nfc_field_decoder_feed(const uint8_t* samples, size_t count) {
    if((!samples && count) || count > NFC_FIELD_SAMPLE_BATCH) return false;
    if(!count) return true;
    if(!s_ntr) {
        s_pos = 0;
        nfc_field_memory.base = nfc_field_memory.samples;
    }
    if((unsigned)s_pos + count > UINT16_MAX) {
        nfc_field_memory.trace.regions_dropped++;
        s_ntr = s_pos = 0;
        nfc_field_memory.base = nfc_field_memory.samples;
    }
    scan_span(samples, (int)count, &s_pos);
    nfc_field_memory.samples += count;
    if(s_ntr >= NFC_FIELD_TRANSITIONS) {
        nfc_field_memory.trace.regions_dropped++;
        s_ntr = s_pos = 0;
        return true;
    }
    if(!s_ntr) return true;
    int cut = 0, position = 0, mv = 0, mn = 0, mp = 0;
    if(s_ntr > 1) cut = find_cut(1, &position, &mv, &mn, &mp);
    if(s_pos - s_tr[s_ntr - 1] >= FRAME_GAP) {
        cut = s_ntr;
        position = s_pos;
    }
    if(cut) {
        int remaining = s_ntr - cut;
        s_ntr = cut;
        sniff_decode(position);
        if(region_valid()) nfc_field_collect(nfc_field_memory.base, false);
        for(int i = 0; i < remaining; i++)
            s_tr[i] = s_tr[i + cut] - (uint16_t)position;
        s_ntr = remaining;
        s_tr_init ^= (cut & 1);
        s_pos -= position;
        nfc_field_memory.base += position;
    }
    return true;
}
void nfc_field_decoder_finish(void) {
    if(s_ntr) {
        sniff_decode(s_pos);
        if(region_valid()) nfc_field_collect(nfc_field_memory.base, true);
        s_ntr = 0;
        s_pos = 0;
    }
}
