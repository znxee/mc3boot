/* ---------------------------------------------------------------------------
 *  mc3_registry.h - one mod calling another mod's function
 *
 *  THE PROBLEM
 *
 *  There is no symbol table and no dynamic linker. Every .mod is relocated
 *  independently to wherever it lands, so a module cannot name a function in
 *  another module at build time - there is nothing to name it with.
 *
 *  Before this, the answer was for both sides to agree on a fixed address and
 *  publish a pointer there by hand. That works, and it does not scale: every
 *  pair of modules needs its own address, the addresses have to be partitioned
 *  by hand, and getting that partition wrong is silent. It has already happened
 *  once here - city_place was placed at MODREPORT+232 believing menu_row's block
 *  was 32 bytes, it had grown to 40, and the two quietly overwrote each other's
 *  data. The symptom reads as "a mod stopped working".
 *
 *  THE REGISTRY
 *
 *  A table of {id, function} that mc3boot allocates at boot, so it exists before
 *  any module runs and no module has to own it:
 *
 *      MC3_EXPORT(MC3_ID('S','P','D','O'), &get_speed);   // in the provider
 *
 *      typedef float (*speed_fn)(void);
 *      speed_fn f = (speed_fn)mc3_import(MC3_ID('S','P','D','O'));
 *      if (f) use(f());                                   // in the consumer
 *
 *  WHY AN ID AND NOT A NAME. A string literal is a data base, and mc3_mkmod
 *  refuses a module whose GCC shared one `lui` across several of them - the
 *  "LO16 sem HI16" error. Character literals are plain integer constants, so
 *  MC3_ID costs nothing at all. It is the same reason mc3_sio.h takes character
 *  codes. Four characters is 32 bits and reads in a log; collisions are the
 *  exporter's problem, so prefix yours.
 *
 *  LOAD ORDER IS THE REAL CONSTRAINT, and the registry does not remove it.
 *
 *  A module can only import what has already been exported, and what has run by
 *  a given moment depends on the .ini: `= 1` modules run from boot, `shim` from
 *  Main's init, `defer` from the first frame. A hooked module exports nothing
 *  until one of its hooks fires, because that is the only time its code runs.
 *
 *  So IMPORT AT THE POINT OF USE, not at load. By the time a per-frame handler
 *  or a menu callback runs, everything has loaded. Importing once at startup and
 *  caching a null is the mistake this warning exists for; cache the result only
 *  after it comes back non-null.
 *
 *  Nothing here is type checked. An id whose signature the two sides disagree
 *  about is a crash, and there is no way to find that out except by agreeing.
 * ------------------------------------------------------------------------- */
#ifndef MC3_REGISTRY_H
#define MC3_REGISTRY_H

/* Four characters into 32 bits. Character literals are integer constants, so
 * this is folded at compile time and adds no data to the module. */
#define MC3_ID(a, b, c, d)                                                    \
    ((mc3_u32)((unsigned char)(a)) << 24 |                                    \
     (mc3_u32)((unsigned char)(b)) << 16 |                                    \
     (mc3_u32)((unsigned char)(c)) << 8  |                                    \
     (mc3_u32)((unsigned char)(d)))

/* The table is described by mc3boot's runtime header. Spelled out rather than
 * included, the way GAME_LO/GAME_HI are in core.cpp - the payload side has no
 * generated header. mc3_inject.py owns the number (SHIMHDR). */
#define MC3_REG_HDR    0x0061D050u
#define MC3_REG_MAGIC  0x4D433353u      /* 'MC3S' */
#define MC3_REG_BASE   (((volatile mc3_u32 *)MC3_REG_HDR)[7])
#define MC3_REG_CAP    (((volatile mc3_u32 *)MC3_REG_HDR)[8])

/* An entry is {id, function}, eight bytes. */
static volatile mc3_u32 *mc3_reg_slot(mc3_u32 id, int make)
{
    const volatile mc3_u32 *h = (const volatile mc3_u32 *)MC3_REG_HDR;
    if (h[0] != MC3_REG_MAGIC)
        return 0;
    const mc3_u32 base = MC3_REG_BASE, cap = MC3_REG_CAP;
    if (!base || !cap)
        return 0;

    volatile mc3_u32 *empty = 0;
    for (mc3_u32 i = 0; i < cap; ++i) {
        volatile mc3_u32 *e = (volatile mc3_u32 *)(base + i * 8u);
        if (e[0] == id)
            return e;                   /* re-exporting replaces */
        if (!e[0] && !empty)
            empty = e;
    }
    return make ? empty : 0;            /* 0 when full, or when not found */
}

/* Publish. Returns 0 if the table is absent or full - check it, because a
 * silent failure here shows up as the consumer finding nothing. */
static int mc3_export(mc3_u32 id, void *fn)
{
    volatile mc3_u32 *e = mc3_reg_slot(id, 1);
    if (!e)
        return 0;
    e[1] = (mc3_u32)fn;
    e[0] = id;                          /* id LAST: it is what makes the entry
                                         * visible, and a reader that catches
                                         * the half-written pair would get a
                                         * live id with a stale pointer */
    return 1;
}

/* Look up. Returns 0 when the provider has not run yet, which is a normal
 * answer and not an error - see the note on load order above. */
static void *mc3_import(mc3_u32 id)
{
    volatile mc3_u32 *e = mc3_reg_slot(id, 0);
    return e ? (void *)e[1] : 0;
}

#endif
