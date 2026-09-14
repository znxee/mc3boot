// -----------------------------------------------------------------------------
//  mc3mod - applies the pnach patch groups from inside the game
//
//  Same writes a .pnach would make, issued by the payload instead. The point is
//  a single binary that behaves identically under PCSX2 and on a real PS2: OPL
//  .cht has no write-once mode and no grouping, so anything beyond a flat list
//  of every-frame codes had no equivalent on hardware until now.
//
//  Three things make this different from just poking memory in a loop:
//
//  1. READ, COMPARE, THEN WRITE. The hooks fire several times per frame, so the
//     work has to be idempotent - and comparing first means the expensive part
//     (the cache flush) happens on the frames where something really changed,
//     normally only the first.
//
//  2. THE INSTRUCTION CACHE. These addresses are code. The R5900 has a 16 KB
//     I-cache and will happily keep running the old words after the store; the
//     recompiler in PCSX2 spots the write and does not. This is exactly the kind
//     of difference that makes a patch "work in the emulator only", so the flush
//     is here and not left to luck.
//
//  3. PARTIAL WRITES ARE KEPT PARTIAL. Some pnach lines write one byte into the
//     low half of an instruction's immediate and rely on the rest of the word
//     already being right. The generator checks that against the ELF and prints
//     the resulting word; here it just has to reproduce the same store width.
// -----------------------------------------------------------------------------

#include "config.h"
#include "patches.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

// Telemetry. Lives in the gap between the loader's state block and the payload,
// so `mc3_inject.py state` can read it out of a savestate without the payload
// having to be parsed. Keep in sync with STATE/PAYLOAD in ../mc3_inject.py.
struct mc3_telemetry {
    u32 magic;        // 'MC3P' once the first pass ran
    u32 groups;       // groups enabled at build time
    u32 writes;       // writes considered per pass
    u32 changed;      // writes that actually altered memory (total, cumulative)
    u32 flushes;      // cache flushes issued
    u32 passes;       // times this ran
    float dt;         // dt of the last pass, to confirm the clock is live
    float rate_smooth;// smoothed frame rate, for MC3_DYNAMIC_CITY_RATE
    u32 rate_applied; // last rate handed to mcCity::SetIntendedFrameRate
};
static mc3_telemetry* const g_tel = (mc3_telemetry*)0x0061C9C0;

#if MC3_DYNAMIC_CITY_RATE
#if (MC3_EN_CIDADE_ORCAMENTO_PARA_60_FPS + MC3_EN_CIDADE_ORCAMENTO_PARA_90_FPS    + MC3_EN_CIDADE_ORCAMENTO_PARA_120_FPS + MC3_EN_CIDADE_ORCAMENTO_PARA_240_FPS    + MC3_EN_CIDADE_ORCAMENTO_PARA_1_FPS) > 0
#error "city rate: pick the dynamic one OR a constant group, not both"
#endif

// mcCity::SetIntendedFrameRate(this, rate). Cheap: it stores two floats and
// divides 60 by the rate. The `this` is the same global mcGame reads at
// 0x1A70C8, and it is null-checked there, so it is null-checked here.
typedef void (*set_city_rate_t)(u32, int);
static const set_city_rate_t g_setCityRate = (set_city_rate_t)0x00258DE8;
static u32* const g_cityObject = (u32*)0x00615B40;

static void update_city_rate(float dt)
{
    // Reachable from the boot hook, where dt is still 0. 1/0 is not a rate.
    if (dt <= 0.0f || dt > 0.5f)
        return;
    // mc3boot.ini can turn this off without a rebuild; with no config block
    // written - the pnach routes never write one - the compiled default stands.
    {
        const u32 magic = *(volatile u32*)0x0061C9F0;
        const u32 flags = *(volatile u32*)0x0061C9F4;
        if (magic == 0x4D433343u && !(flags & 2u))
            return;
    }

    // Raw 1/dt jitters by a frame either way, and rounding it would flip
    // between 119 and 120 forever, calling the setter every frame. Smooth
    // first, then require a real move before acting.
    const float now = 1.0f / dt;
    float s = g_tel->rate_smooth;
    if (s < 1.0f || s > 1000.0f)
        s = now;                       // first pass, or nonsense
    s += (now - s) * 0.05f;
    g_tel->rate_smooth = s;

    int rate = (int)(s + 0.5f);
    if (rate < 10)  rate = 10;
    if (rate > 240) rate = 240;

    const int applied = (int)g_tel->rate_applied;
    int move = rate - applied;
    if (move < 0) move = -move;
    if (applied != 0 && move < 2)
        return;

    const u32 city = *g_cityObject;
    if (city < 0x00100000u || city >= 0x02000000u || (city & 3u))
        return;
    g_setCityRate(city, rate);
    g_tel->rate_applied = (u32)rate;
}
#endif

#if MC3_FLUSH_CACHE
// EE kernel syscall 100. mode 0 writes back the data cache, mode 2 invalidates
// the instruction cache; after storing code you want both, in that order.
static void ee_flush_cache(int mode)
{
    asm volatile(
        "move  $4, %0  \n"
        "li    $3, 100 \n"
        "syscall       \n"
        :
        : "r"(mode)
        : "$2", "$3", "$4", "$5", "$6", "$7", "memory");
}
#endif

// Returns non-zero if the store changed anything.
static int poke(u32 addr, u32 value, u8 size)
{
    switch (size) {
    case 1: {
        volatile u8* p = (volatile u8*)addr;
        if (*p == (u8)value) return 0;
        *p = (u8)value;
        return 1;
    }
    case 2: {
        volatile u16* p = (volatile u16*)addr;
        if (*p == (u16)value) return 0;
        *p = (u16)value;
        return 1;
    }
    default: {
        volatile u32* p = (volatile u32*)addr;
        if (*p == value) return 0;
        *p = value;
        return 1;
    }
    }
}

extern "C" void mc3_apply_patches(float dt)
{
    u32 seen = 0, changed = 0, groups = 0;

    for (int g = 0; g < MC3_NGROUPS; ++g) {
        const mc3_group& G = mc3_groups[g];
        if (!G.enabled)
            continue;
        ++groups;
        for (u16 i = 0; i < G.n; ++i) {
            ++seen;
            changed += (u32)poke(G.w[i].addr, G.w[i].value, G.w[i].size);
        }
    }

    if (changed) {
#if MC3_FLUSH_CACHE
        ee_flush_cache(0);        // write back the data cache
        ee_flush_cache(2);        // invalidate the instruction cache
#endif
        g_tel->flushes += 1;
    }

    g_tel->magic   = 0x4D433350;   // 'MC3P'
    g_tel->groups  = groups;
    g_tel->writes  = seen;
    g_tel->changed += changed;
    g_tel->passes += 1;
    g_tel->dt      = dt;

#if MC3_DYNAMIC_CITY_RATE
    update_city_rate(dt);         // no-ops until the clock is live
#endif
}
