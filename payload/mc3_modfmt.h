/* ---------------------------------------------------------------------------
 *  mc3_modfmt.h - placing a .mod, shared by both things that do it.
 *
 *  mc3boot places modules before the game starts. The core module places them
 *  after it has started, into memory the game handed out. The parsing and the
 *  relocation are identical, and having them in two files is how the two drift
 *  apart - so they live here and both include it.
 *
 *  Format, produced by mc3_mkmod.py:
 *
 *      +0   'MC3M'
 *      +4   version (2)
 *      +8   entry offset
 *      +12  code size
 *      +16  relocation count
 *      +20  link base
 *      +24  hook table offset, 0 if none
 *      +28  hook count
 *      +32  code
 *           relocations, 12 bytes each: [type][off1][off2]
 * ------------------------------------------------------------------------- */
#ifndef MC3_MODFMT_H
#define MC3_MODFMT_H

#define MC3M_MAGIC   0x4D43334Du
#define MC3M_VERSION 2
#define MC3M_HDR     32

#define MC3M_T_WORD32 1
#define MC3M_T_JAL26  2
#define MC3M_T_HI_LO  3

enum {
    MC3M_OK = 0, MC3M_ERR_MAGIC, MC3M_ERR_VERSION, MC3M_ERR_TRUNC
};

struct mc3m_module {
    unsigned int base;      /* where the code was placed */
    unsigned int entry;     /* absolute entry, or base if it has none */
    unsigned int hooks;     /* absolute address of the hook table, 0 if none */
    unsigned int nhooks;
    unsigned int size;      /* code size */
};

/* Copies the code to `dest` and replays the relocations against it. `file` is
 * the whole .mod as read from disk, `len` its length.
 *
 * The hook table is read by the caller AFTER this returns: its handler pointers
 * are ordinary absolute addresses carrying R_MIPS_32, so they are fixed up by
 * the same loop as everything else and need no special case. */
static int mc3m_place(const void *file, unsigned int len,
                      unsigned int dest, struct mc3m_module *out)
{
    const unsigned int *h = (const unsigned int *)file;
    const unsigned char *bytes = (const unsigned char *)file;
    unsigned int entry, size, nrel, base, hoff, hn, delta, i;

    if (len < MC3M_HDR || h[0] != MC3M_MAGIC)
        return MC3M_ERR_MAGIC;
    if (h[1] != MC3M_VERSION)
        return MC3M_ERR_VERSION;
    entry = h[2]; size = h[3]; nrel = h[4]; base = h[5]; hoff = h[6]; hn = h[7];
    if (len < MC3M_HDR + size + nrel * 12u)
        return MC3M_ERR_TRUNC;

    for (i = 0; i < size; ++i)
        ((volatile unsigned char *)dest)[i] = bytes[MC3M_HDR + i];

    delta = dest - base;
    for (i = 0; i < nrel; ++i) {
        const unsigned int *r =
            (const unsigned int *)(bytes + MC3M_HDR + size + i * 12u);
        volatile unsigned int *p = (volatile unsigned int *)(dest + r[1]);
        switch (r[0]) {
        case MC3M_T_WORD32:
            *p += delta;
            break;
        case MC3M_T_JAL26: {
            unsigned int w = *p;
            unsigned int t = ((w & 0x3FFFFFFu) << 2) | (dest & 0xF0000000u);
            t += delta;
            *p = (w & 0xFC000000u) | ((t >> 2) & 0x3FFFFFFu);
            break;
        }
        case MC3M_T_HI_LO: {
            volatile unsigned int *q = (volatile unsigned int *)(dest + r[2]);
            int low = (int)(*q & 0xFFFFu);
            unsigned int v, nl, nh;
            if (low & 0x8000) low -= 0x10000;
            v = (((*p & 0xFFFFu) << 16) + (unsigned int)low) + delta;
            nl = v & 0xFFFFu;
            /* The low half is sign extended when the pair is added back
             * together, so the high half has to carry the borrow. */
            nh = ((v - (unsigned int)(int)(short)nl) >> 16) & 0xFFFFu;
            *p = (*p & 0xFFFF0000u) | nh;
            *q = (*q & 0xFFFF0000u) | nl;
            break;
        }
        default:
            break;      /* unknown types are refused at build time */
        }
    }

    out->base = dest;
    out->entry = dest + entry;
    out->hooks = hn ? (dest + hoff) : 0u;
    out->nhooks = hn;
    out->size = size;
    return MC3M_OK;
}

/* The shim table mc3boot leaves behind for `= shim` modules. Each record is
 * {code[4], slot, alvo}: the boot loader already redirected the game word to
 * the shim and parked the ORIGINAL target in the slot, so a site listed here is
 * one where the handler belongs in the slot and the game word must be left
 * exactly as it is. See the block comment in boot/mc3boot.c.
 *
 * A table that is absent or unrecognised means no module asked for a shim this
 * boot, and every hook takes the direct route below - which is what makes this
 * safe to call from a payload running against an older loader.
 *
 * The address is spelled out rather than included, the way GAME_LO/GAME_HI are
 * in core.cpp - the payload side has no generated header. mc3_inject.py owns
 * the number (SHIMHDR); if it moves, this and boot/addresses.h both follow. */
#define MC3M_SHIMHDR  0x0061D050u
#define MC3M_SHIM_MAGIC 0x4D433353u

/* How many hooks were routed through a slot rather than by patching the game
 * word. It is the one number saying the two halves of the scheme met: boot
 * installed a shim for a site and the body found it again.
 *
 * It lives in the shim header at +16 rather than in a static of this module,
 * and that is not an aesthetic choice - a new static here is a second data base
 * in whatever module includes the header, and mc3_mkmod refuses the build with
 * "LO16 sem HI16" when GCC shares one `lui` between two of them (measured, core
 * at +62C). A fixed absolute address costs nothing: GCC builds 0x0061D050 with
 * lui/ori and no relocation at all. */
#define MC3M_SHIM_HITS (((volatile unsigned int *)MC3M_SHIMHDR)[4])

static volatile unsigned int *mc3m_shim_slot(unsigned int target)
{
    const volatile unsigned int *h =
        (const volatile unsigned int *)MC3M_SHIMHDR;
    unsigned int i, base, stride;
    if (h[0] != MC3M_SHIM_MAGIC || !h[1])
        return 0;
    base = h[2];
    stride = h[3];
    if (!base || !stride)
        return 0;
    for (i = 0; i < h[1]; ++i) {
        const unsigned int rec = base + i * stride;
        if (*(const volatile unsigned int *)(rec + 20) == target)
            return (volatile unsigned int *)(rec + 16);
    }
    return 0;
}

/* Writes `jal handler` over each target named in the module's hook table, or
 * into the shim's slot when boot took that site already.
 * Returns how many were installed. Targets outside the game image are refused:
 * a bad address here is a jump into nothing on the first frame. */
static int mc3m_install_hooks(const struct mc3m_module *m,
                              unsigned int img_lo, unsigned int img_hi)
{
    const unsigned int *tab = (const unsigned int *)m->hooks;
    unsigned int i;
    int n = 0;
    if (!m->hooks)
        return 0;
    for (i = 0; i < m->nhooks; ++i) {
        unsigned int target = tab[i * 2];
        unsigned int fn = tab[i * 2 + 1];
        volatile unsigned int *slot;
        if (target < img_lo || target >= img_hi || (target & 3u))
            continue;
        slot = mc3m_shim_slot(target);
        if (slot) {
            *slot = fn;                 /* the game word already points here */
            MC3M_SHIM_HITS += 1u;
        } else
            *(volatile unsigned int *)target =
                0x0C000000u | ((fn >> 2) & 0x3FFFFFFu);
        ++n;
    }
    return n;
}

#endif
