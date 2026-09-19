/* ---------------------------------------------------------------------------
 *  mc3_raceconfig.h - name -> index for the fields of an mcRaceConfig,
 *  WITHOUT the game's own setters.
 *
 *  mcRaceConfig::SetCity/SetTOD/SetWeather/SetRaceType all walk a fixed table
 *  with strcasecmp and store the matching index - and every one of them ends
 *  in `while (1);` when nothing matches (retail's compiled-out assert).
 *  Handing one a name straight from the .ini is therefore a hang the moment
 *  somebody types it wrong. These do the same walk, return -1 instead, and
 *  leave the store to the caller.
 *
 *  Tables, read out of the executable:
 *      TOD       0x00619B48, 3  (dawn, midnight, dusk)         field +4
 *      WEATHER   0x00619B58, 3  (clear, cloudy, rainy)         field +8
 *      RACETYPE  0x00619C20, 23 (roam .. ordered_track)        field +0x18
 *      CITY      *0x00619D4C, stride 76, name at +0            field +0
 * ------------------------------------------------------------------------- */
#ifndef MC3_RACECONFIG_H
#define MC3_RACECONFIG_H

#include "mc3_mod.h"

#define MC3_RC_CURRENT   0x00619B10u   /* mcRaceConfig* current */
#define MC3_RC_NEXT      0x00619B14u   /* mcRaceConfig* next    */

#define MC3_RC_F_CITY     0x00u
#define MC3_RC_F_TOD      0x04u
#define MC3_RC_F_WEATHER  0x08u
#define MC3_RC_F_RACETYPE 0x18u

#define MC3_RC_T_TOD      0x00619B48u
#define MC3_RC_N_TOD      3
#define MC3_RC_T_WEATHER  0x00619B58u
#define MC3_RC_N_WEATHER  3
#define MC3_RC_T_RACETYPE 0x00619C20u
#define MC3_RC_N_RACETYPE 23

#define MC3_RC_CITY_ARRAY  0x00619D4Cu
#define MC3_RC_CITY_STRIDE 76u
#define MC3_RC_CITY_MAX    8
#define MC3_RC_CITY_EMPTY  0x0066A492u  /* "not_initialized", the constructor's name */

#define MC3_RC_STRCASECMP  0x004328A0u

static int mc3_rc_ptr_ok(mc3_u32 p) { return p >= 0x00100000u && p < 0x02000000u; }

/* A table of `count` char* at `table`: index of `name`, or -1. */
static int mc3_rc_lookup(mc3_u32 table, int count, const char *name)
{
    int i;
    for (i = 0; i < count; ++i) {
        const mc3_u32 s = *(volatile mc3_u32 *)(table + (mc3_u32)i * 4u);
        if (!mc3_rc_ptr_ok(s))
            return -1;
        if (MC3_CALL2(int, MC3_RC_STRCASECMP, const char *, const char *)
                ((const char *)s, name) == 0)
            return i;
    }
    return -1;
}

/* The city table is records, not pointers, and ends at the first unregistered
 * one. Name pointers are NOT 4-aligned (packed string table) - range check only. */
static int mc3_rc_lookup_city(const char *name)
{
    const mc3_u32 base = *(volatile mc3_u32 *)MC3_RC_CITY_ARRAY;
    int i;
    if (!mc3_rc_ptr_ok(base) || (base & 3u))
        return -1;
    for (i = 0; i < MC3_RC_CITY_MAX; ++i) {
        const mc3_u32 np = *(volatile mc3_u32 *)(base + (mc3_u32)i * MC3_RC_CITY_STRIDE);
        if (np == MC3_RC_CITY_EMPTY || !mc3_rc_ptr_ok(np))
            return -1;
        if (MC3_CALL2(int, MC3_RC_STRCASECMP, const char *, const char *)
                ((const char *)np, name) == 0)
            return i;
    }
    return -1;
}

/* Writes `value` into `field` of the config `cfg_ptr` points at, if it is built. */
static void mc3_rc_store(mc3_u32 cfg_ptr, mc3_u32 field, mc3_u32 value)
{
    const mc3_u32 cfg = *(volatile mc3_u32 *)cfg_ptr;
    if (mc3_rc_ptr_ok(cfg) && !(cfg & 3u))
        *(volatile mc3_u32 *)(cfg + field) = value;
}

#endif
