/* ---------------------------------------------------------------------------
 *  mc3_bootargs.h - read the [boot] section of mc3boot.ini from inside a mod
 *
 *  THE PROBLEM
 *
 *  Testing one specific thing - boot straight into modcity, boot with a given
 *  car selected, skip past a menu - used to mean a constant baked into a
 *  .cpp, a rebuild, and a copy to HostFS, for every single change. The .ini is
 *  already read before any module runs; this lets a module ask it a question
 *  instead of hard-coding the answer.
 *
 *      [boot]
 *      city = modcity
 *      car  = vp_eclipse_04
 *
 *      const char *city = mc3_bootarg(MC3_ID('c','i','t','y'));
 *      if (city) { ... }             // NULL: the key was not set this boot
 *
 *  WHY THE KEY IS FOUR CHARACTERS, not the string "city"
 *
 *  A string literal in a .mod is a data base, and mc3_mkmod refuses a build
 *  where GCC shares one `lui` across several - see docs/WRITING_A_MOD.md.
 *  MC3_ID (from mc3_registry.h) is already the fix for exactly this, so
 *  boot-args reuses it rather than inventing a second convention: an .ini key
 *  is folded into the same four bytes by mc3boot.c keeping only its first
 *  four characters, so `city = modcity` and MC3_ID('c','i','t','y') name the
 *  same slot. A key past four characters still works in the .ini - only the
 *  first four decide which slot it lands in, so keep keys short and distinct
 *  in that prefix.
 *
 *  THE VALUE IS SAFE TO READ AS A STRING, unlike a literal you would write
 *  yourself: it lives in the cave, written by mc3boot.c (which links a real
 *  libc - it is the standalone loader, not a freestanding .mod) before any
 *  module ran, and mc3_bootarg hands back a pointer straight into that table.
 *  Nothing about reading it creates a data base in your own module - only
 *  authoring a literal does.
 *
 *  WHEN A KEY IS NOT SET, mc3_bootarg returns NULL. That is the ordinary case
 *  when nobody wrote [boot] at all - a module reading a boot-arg should always
 *  have a compiled-in default to fall back to, the way draw_distance falls
 *  back to LEVEL_DEFAULT when the memory card holds nothing recognisable.
 *
 *  See mods/city_force for a working example: it reads `[boot] city`, and
 *  falls back to its own compiled-in TARGET_CITY when the key is absent.
 * ------------------------------------------------------------------------- */
#ifndef MC3_BOOTARGS_H
#define MC3_BOOTARGS_H

#include "mc3_registry.h"   /* for MC3_ID and the shared header layout */

#define MC3_ARGS_VALLEN 32u
#define MC3_ARGS_STRIDE (4u + MC3_ARGS_VALLEN)
#define MC3_ARGS_BASE   (((volatile mc3_u32 *)MC3_REG_HDR)[9])
#define MC3_ARGS_N      (((volatile mc3_u32 *)MC3_REG_HDR)[10])

/* Returns a pointer to the value's first byte in the cave, or 0 if the key was
 * never set this boot (no [boot] section, or this key absent from it). The
 * string is NUL-terminated and at most MC3_ARGS_VALLEN-1 bytes; mc3boot.c
 * truncates anything longer when it writes the table. */
static const char *mc3_bootarg(mc3_u32 id)
{
    const volatile mc3_u32 *h = (const volatile mc3_u32 *)MC3_REG_HDR;
    mc3_u32 base, i;
    if (h[0] != MC3_REG_MAGIC)
        return 0;
    base = MC3_ARGS_BASE;
    if (!base)
        return 0;
    /* Capacity, not MC3_ARGS_N: N counts how many are SET, which can be lower
     * than the table's size, but a slot's own id (0 = free) is what actually
     * tells a search where to stop looking. 16 is boot/mc3boot.c's ARGS_CAP;
     * duplicated here because the payload side has no generated header. */
    for (i = 0; i < 16u; ++i) {
        const volatile mc3_u32 *rec =
            (const volatile mc3_u32 *)(base + i * MC3_ARGS_STRIDE);
        if (*rec == id)
            return (const char *)rec + 4;
        if (*rec == 0)
            break;   /* slots are packed from the front; a free one ends the search */
    }
    return 0;
}

#endif
