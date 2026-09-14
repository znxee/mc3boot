/* ---------------------------------------------------------------------------
 *  mc3_log.h - a log any mod can write to, that survives a hang.  ABI v2.
 *
 *  THERE IS NO BUILT-IN ONE TO TURN ON. The retail build has no Displayf, no
 *  Printf, no `file(line)` assert format and no `.log` anywhere in the string
 *  pool - the debug output was compiled out, the same way the `.hood` and
 *  `.lvl` readers were. So this is it.
 *
 *  WHY v2, AND WHAT WAS WRONG WITH v1
 *
 *  v1 put its head at a fixed address, 0x0061CFF0, carved out of the report
 *  window. Fork City MODS 2 found that this COLLIDES with proper_widescreen's
 *  MC3W report, which is live - so the logger could not be turned on in the
 *  chain that most needed it. That is the third collision this project has had
 *  in that window, and the lesson is not "pick a better address": with several
 *  forks shipping modules independently, no fixed address is safe.
 *
 *  So the head now lives in each module's OWN .data, which the loader
 *  relocates, and the reader FINDS the heads by scanning for the magic. Any
 *  number of modules can log, each into its own ring, with no address to
 *  coordinate and nothing to collide with.
 *
 *      python modloader/mc3_inject.py state "savestate.p2s"
 *
 *  lists every ring it finds, with its owner tag.
 *
 *  USING IT
 *
 *      #define MC3_LOG_OWNER 'PLAC'        // four characters, your module
 *      #include "mc3_log.h"
 *
 *      MC3_LOG("meshes resolved");
 *      MC3_LOG1("model", ptr);                     one value, hex
 *      MC3_LOG2("slot/model", i, ptr);
 *      MC3_LOG4("ctx/group/vtbl/slots", a, b, c, d);
 *      MC3_LOGD("count", n);                       decimal
 *
 *  There is no printf and there should not be: a module has no libc, and a
 *  format parser is a thing that can itself crash while you are trying to find
 *  out what crashed.
 *
 *  TWO RULES THAT COME FROM REAL FAILURES
 *
 *  WRITE THE LINE BEFORE THE RISKY CALL, not after. A line saying "about to
 *  load X" with no line after it NAMES the X that never returned. A line
 *  written after a call only ever describes calls that came back. That is how
 *  a mesh was named as the thing that hung, and how gfxGetModel was isolated
 *  after three wrong explanations.
 *
 *  A MODULE WITH MC3_HOOK NEVER GETS mod_main. The loader gives the per-frame
 *  entry only to modules that declare no hooks - Fork City MODS 2 lost a build
 *  to this. If your module has hooks, initialise from the first hook, not from
 *  an entry that will not be called. Logging is safe either way: the ring is
 *  created by whoever logs first.
 * ------------------------------------------------------------------------- */
#ifndef MC3_LOG_H
#define MC3_LOG_H

#include "mc3_mod.h"

/* 'MC3K'. NOT 'MC3P' - that is the payload's own signature word, and a
 * magic that already means something else is an invitation to a confusing
 * false positive. The magics in use are listed in mc3_inject.py. */
#define MC3_LOG_MAGIC   0x4D43334Bu
#define MC3_LOG_VERSION 2u
#define MC3_LOG_SIZE    0x8000u         /* 32 KB of ring, from the game heap */

#ifndef MC3_LOG_OWNER
#define MC3_LOG_OWNER   0x3F3F3F3Fu     /* '????' - name your module */
#endif

/* Found by scanning, so every field the reader needs to trust it is here.
 * `size` and `version` are as much a checksum as a value: a random word that
 * happens to equal the magic will not also carry a sane size. */
struct mc3_log_head {
    mc3_u32 magic;
    mc3_u32 version;
    mc3_u32 owner;      /* four characters, whose ring this is */
    mc3_u32 ring;       /* the buffer, on the game heap; 0 until first use */
    mc3_u32 size;
    mc3_u32 cursor;     /* bytes written since the ring was created */
    mc3_u32 sequence;   /* records written, so a wrap is countable */
    mc3_u32 dropped;    /* records lost to a failed allocation */
};

/* One per module, in its own .data. `used` keeps it through -O2. */
__attribute__((section(".data"), used))
static struct mc3_log_head mc3_log_head_v2 = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
};

/* NOT inline, and neither is the writer. Logging from several places gives GCC
 * the chance to hoist one `lui` and feed every access from it, which the module
 * format cannot relocate - mc3_mkmod refuses the build with "LO16 sem HI16".
 * One real function keeps the address arithmetic in one place. */
static __attribute__((noinline)) struct mc3_log_head *mc3_log_ptr(void)
{
    return &mc3_log_head_v2;
}

static __attribute__((noinline)) void mc3_log_put(const char *texto, mc3_u32 n,
                                                  mc3_u32 a, mc3_u32 b,
                                                  mc3_u32 c, mc3_u32 e,
                                                  int decimal)
{
    struct mc3_log_head *h = mc3_log_ptr();

    if (h->magic != MC3_LOG_MAGIC) {
        /* Lazy: the ring costs 32 KB and only a module that actually logs
         * should pay it. mc3_alloc needs the game heap up, which is why this
         * happens on the first line rather than at load time. */
        void *r = mc3_alloc(MC3_LOG_SIZE);
        if (!r) {
            h->dropped += 1u;               /* no log is better than a crash */
            return;
        }
        h->ring = (mc3_u32)r;
        h->size = MC3_LOG_SIZE;
        h->cursor = 0u;
        h->sequence = 0u;
        h->owner = MC3_LOG_OWNER;
        h->version = MC3_LOG_VERSION;
        h->magic = MC3_LOG_MAGIC;           /* published LAST, see below */
    }
    if (!h->ring)
        return;

    /* Record: [u16 total][u8 nvals][u8 decimal][text NUL][values] */
    mc3_u32 len = 0u;
    while (texto[len] && len < 56u)
        ++len;
    const mc3_u32 total = 4u + len + 1u + n * 4u;

    mc3_u32 pos = h->cursor % h->size;
    if (pos + total > h->size) {            /* wrap: skip the tail, restart */
        h->cursor += h->size - pos;
        pos = 0u;
    }
    mc3_u8 *p = (mc3_u8 *)h->ring + pos;
    /* Body first, header word last. A savestate taken mid-write then shows a
     * zero length and the reader stops there, instead of walking into a record
     * that is half old and half new. */
    for (mc3_u32 i = 0u; i < len; ++i)
        p[4 + i] = (mc3_u8)texto[i];
    p[4 + len] = 0u;

    mc3_u8 *v = p + 4u + len + 1u;
    const mc3_u32 vals[4] = { a, b, c, e };
    for (mc3_u32 i = 0u; i < n; ++i) {
        v[i * 4 + 0] = (mc3_u8)(vals[i] & 0xFFu);
        v[i * 4 + 1] = (mc3_u8)((vals[i] >> 8) & 0xFFu);
        v[i * 4 + 2] = (mc3_u8)((vals[i] >> 16) & 0xFFu);
        v[i * 4 + 3] = (mc3_u8)(vals[i] >> 24);
    }
    p[2] = (mc3_u8)n;
    p[3] = (mc3_u8)decimal;
    p[0] = (mc3_u8)(total & 0xFFu);         /* the commit */
    p[1] = (mc3_u8)(total >> 8);

    h->cursor += total;
    h->sequence += 1u;
}

#define MC3_LOG(t)        mc3_log_put((t), 0u, 0u, 0u, 0u, 0u, 0)
#define MC3_LOG1(t, a)    mc3_log_put((t), 1u, (mc3_u32)(a), 0u, 0u, 0u, 0)
#define MC3_LOG2(t, a, b) mc3_log_put((t), 2u, (mc3_u32)(a), (mc3_u32)(b), \
                                      0u, 0u, 0)
#define MC3_LOG4(t, a, b, c, d) \
    mc3_log_put((t), 4u, (mc3_u32)(a), (mc3_u32)(b), (mc3_u32)(c), \
                (mc3_u32)(d), 0)
#define MC3_LOGD(t, a)    mc3_log_put((t), 1u, (mc3_u32)(a), 0u, 0u, 0u, 1)

#endif
