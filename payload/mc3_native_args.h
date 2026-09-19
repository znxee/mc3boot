/* ---------------------------------------------------------------------------
 *  mc3_native_args.h - hand [boot] to the game's OWN datArgParser
 *
 *  mc3_bootargs.h already lets a .mod ask "what did [boot] set for key X",
 *  which is enough for a .mod written against this project. It is not enough
 *  to drive datArgParser::Get, because that call hashes the FULL string the
 *  retail executable already ships with ("maxopponents"), and bootarg_set
 *  folds a key down to four characters ("maxo") to dodge the LO16/HI16 data
 *  base trap on the .mod side - fine for this project's own lookups, wrong
 *  for the game's.
 *
 *  WHAT SURVIVES IN RETAIL, MEASURED BY DISASSEMBLY
 *
 *  datArgParser::Init(int argc, char **argv)  0x00428AC0 - walks argv, and for
 *      every entry starting with '-' inserts (via HashTable::Access) the text
 *      after '-' into ArgHash, splitting off "=value" at the first '=' it
 *      finds with strchr. Ordinary heap allocation (__builtin_new) - nothing
 *      about it needs boot-time memory conditions a shim mod does not have.
 *  datArgParser::Get(const char *key)         0x004293F0 - a thin wrapper
 *      that calls the two-argument indexed form with index 0xFFFF ("match
 *      the key regardless of index"), which is the same shape the alpha's
 *      GetNum/car1/car2 indexed args used - that indexing survives.
 *
 *  What does NOT survive is a data SOURCE (ResponseFile, 0x001A0CE8, is an
 *  empty stub in retail: `jr ra; nop` - argc/argv reaching Init at boot are
 *  whatever the PS2 crt0 hands it, normally nothing) and CONSUMERS (nothing
 *  in retail's .rodata contains "nofe", "car1", etc., so nothing calls Get()
 *  with any game-specific flag - the vocabulary the alpha built on top of
 *  this parser is gone, only the parser itself is not).
 *
 *  So there is nothing to "port" from the alpha - this is retail's own,
 *  still-functional Init/Get, called a second time, fed argv built from
 *  [boot] instead of an empty one from crt0. mc3boot.c already builds that
 *  argv in the cave (see reserve_nativeargs/nativearg_add in boot/mc3boot.c),
 *  in exactly the "-key" / "-key=value" shape Init's own loop expects - a
 *  .mod only has to hand the two numbers below to Init once.
 *
 *  mods/native_bootargs is the worked example: a hookless, run-once module
 *  that calls Init(argc, argv) on the first frame, then proves it worked by
 *  calling Get() on its own argv[0] (skip the leading '-') - reusing a
 *  pointer that already lives in the cave rather than authoring a new string
 *  literal, which would be another LO16/HI16 data base for no reason.
 * ------------------------------------------------------------------------- */
#ifndef MC3_NATIVE_ARGS_H
#define MC3_NATIVE_ARGS_H

#include "mc3_registry.h"   /* for MC3_REG_HDR / MC3_REG_MAGIC and mc3_u32 */

/* Index 11, not 12: SHIMHDR..MODTAB is a fixed 48-byte gap in the cave (see
 * mc3_inject.py), and the struct on the boot side already used all but its
 * last word before this field existed - there was no room for a second one
 * (argc) beside it. Measured directly: an earlier version of this file also
 * defined index 12 as an argc field, and it read back as whatever MODTAB's
 * first entry happened to hold, not what mc3boot.c had written. So argc is
 * never stored - argv[] is zero-terminated instead, one more entry than its
 * advertised capacity, and a reader counts up to the terminator itself. */
#define MC3_ARGV_BASE     (((volatile mc3_u32 *)MC3_REG_HDR)[11])
#define MC3_ARGV_CAP      16u

/* Returns argc and *argv_out = the argv[] base, or 0 if [boot] set nothing
 * (an empty [boot] section, or none at all - the ordinary case). Both are
 * addresses into the cave built by mc3boot.c; nothing here allocates. */
static mc3_u32 mc3_native_argv(mc3_u32 *argv_out)
{
    const volatile mc3_u32 *h = (const volatile mc3_u32 *)MC3_REG_HDR;
    mc3_u32 base, n;
    if (h[0] != MC3_REG_MAGIC || !MC3_ARGV_BASE) {
        *argv_out = 0;
        return 0;
    }
    base = MC3_ARGV_BASE;
    for (n = 0; n < MC3_ARGV_CAP && ((const volatile mc3_u32 *)base)[n]; ++n)
        ;
    *argv_out = base;
    return n;
}

#endif
