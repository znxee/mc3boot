// -----------------------------------------------------------------------------
//  mc3mod - external payload, loaded at run time by the patch-file loader
//
//  Built with the ps2dev toolchain, linked at the cave address, extracted as a
//  raw binary, and either read by the game through its own Stream API or carried
//  inside the patch file. See build.sh and ../README.md.
//
//  WHAT THIS EXAMPLE DOES, and why it justifies the whole apparatus:
//
//  Pedestrian animation advances by a playback rate in FRAMES PER TICK
//  (mcCreatureAnimator +0x10). With no `dt` in it, doubling the frame rate
//  doubles their speed. A constant patch fixes that by halving the rate, which
//  is only right at exactly 60 fps: at 30 they crawl, and at 45 nothing fits.
//
//  Here the arithmetic is the right one: rate = base * dt * 30. At 30 fps that
//  is base * 1.0, at 60 base * 0.5, and at any other rate whatever that frame
//  asks for. This does NOT fit in a constant patch, because it depends on a
//  value that only exists at run time. That is the reason a payload exists.
//
//  Verified in game at 120.47 Hz: 14 of 14 live pedestrians at exactly the
//  expected ratio of 0.2490, each one's own variation preserved.
//
//  This REPLACES the ten constant rate lines of the "60 FPS anim" patch. Both
//  doing it at once would halve twice - so it is not left to memory: turning on
//  both MC3_DYNAMIC_PEDS and the constant group is a compile error, below.
//
//  Since patches.cpp arrived, the payload also applies the rest of the pnach
//  groups itself. See config.h for what is on.
// -----------------------------------------------------------------------------

#include "config.h"
#include "mc3_mod.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern "C" void mc3_apply_patches(float dt);

#if MC3_DYNAMIC_PEDS && MC3_EN_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS
#error "pedestres: escolha o dinamico OU o grupo constante, nao os dois"
#endif

// Runtime feature flags, written by the chain loader from mc3boot.ini. When the
// block is absent - the pnach routes never write it - the compiled-in default
// stands, so an older setup keeps behaving exactly as before.
//
// Keep in sync with CFG/CFG_MAGIC in ../mc3_inject.py.
struct mc3_config { u32 magic; u32 flags; };
static const mc3_config* const g_cfg = (const mc3_config*)0x0061C9F0;
enum { F_DYNAMIC_PEDS = 1u, F_DYNAMIC_CITY_RATE = 2u };

static inline bool feature(u32 bit, bool padrao)
{
    if (g_cfg->magic != 0x4D433343u)      // 'MC3C'
        return padrao;
    return (g_cfg->flags & bit) != 0u;
}

// --- SLUS-21355 addresses, from the symbol table in ../../simbolos/ -----------
static float* const g_frameDt = (float*)0x00618E20;   // datTimeManager's dt
static u32* const   g_simSlot = (u32*)0x00619494;     // pointer to pointer

// offsets confirmed by reading savestates
enum {
    SIM_MANAGER   = 152,      // mcCityLifeSimulatorData -> group manager
    GROUP_A       = 4,
    GROUP_B       = 40,
    GROUP_ARRAY   = 0,        // pointer to the creature array
    GROUP_COUNT   = 4,        // u16 count
    CREATURE_ANIM = 216,      // -> mcCreatureAnimator
    CREATURE_RATE = 0x15C,    // base rate built by mcCreature::Init
    ANIM_RATE     = 0x10,     // frames per tick
    ANIM_PRESET_A = 0x14,     // copied back into +0x10 when the animation changes
    ANIM_PRESET_B = 0x18,
};

static inline bool valid_ram(u32 p)
{
    // A plausible aligned pointer into EE RAM. Cheap, and enough to avoid
    // following garbage while the city is still loading.
    return p >= 0x00100000u && p < 0x02000000u && (p & 3u) == 0u;
}

// The signature sits in word 0 of the binary, placed there by the linker script.
// The loader only jumps when it matches, so clobbered memory becomes a jump NOT
// TAKEN instead of a hang.
__attribute__((section(".magic"), used))
extern const u32 g_signature = 0x4D433350;

// -----------------------------------------------------------------------------
//  Entry point. The loader calls this once per frame from inside mcGame, right
//  after datTimeManager::Update - so the frame's dt is already computed.
//
//  Must be IDEMPOTENT: more than one hook is live at a time, so this runs
//  several times per frame. Assigning a value is safe; accumulating is not.
// -----------------------------------------------------------------------------
extern "C" void payload_main() __attribute__((section(".text.start")));
extern "C" void payload_main()
{
    const float dt = *g_frameDt;

    // The patch groups run BEFORE the clock is checked, and that ordering is the
    // whole reason the early hook can work: they are plain memory writes with no
    // dependency on dt, on the city, or on anything being initialised. Called
    // from the boot hook, dt is still 0 and this is the only part that can run -
    // which is exactly the part that has to run before the game reads those
    // words. Everything below needs a live frame.
    mc3_apply_patches(dt);

    if (dt <= 0.0f || dt > 0.5f)          // clock not ready yet
        return;

    // The compiled-in value is the default; mc3boot.ini can turn it off without
    // a rebuild. Kept behind the #if as well, so a payload built without this
    // feature carries none of its code.
#if MC3_DYNAMIC_PEDS
    if (!feature(F_DYNAMIC_PEDS, true))
        return;

    // the same scale the constant patches apply, but computed live:
    //   30 fps -> 1.0     60 fps -> 0.5     any other -> whatever it asks for
    const float scale = dt * 30.0f;

    const u32 slot = *g_simSlot;
    if (!valid_ram(slot))
        return;
    const u32 sim = *(u32*)slot;
    if (!valid_ram(sim))
        return;

    const u32 manager = sim + SIM_MANAGER;
    const int groups[2] = { GROUP_A, GROUP_B };

    for (int g = 0; g < 2; ++g) {
        const u32 group = manager + groups[g];
        const u32 arr = *(u32*)(group + GROUP_ARRAY);
        const u16 n   = *(u16*)(group + GROUP_COUNT);
        if (!valid_ram(arr) || n == 0 || n > 512)
            continue;

        for (u16 i = 0; i < n; ++i) {
            const u32 c = *(u32*)(arr + 4u * i);
            if (!valid_ram(c))
                continue;
            const u32 anim = *(u32*)(c + CREATURE_ANIM);
            if (!valid_ram(anim))
                continue;

            const float base = *(float*)(c + CREATURE_RATE);
            if (base <= 0.0f || base > 8.0f)
                continue;

            const float rate = base * scale;
            // All three together: +0x14 and +0x18 are presets that
            // mcCreatureAnimator::SetAnimation and SetToPredictedFrameData copy
            // back into +0x10 when the animation changes. Touching only +0x10
            // would be undone at the first transition.
            *(float*)(anim + ANIM_RATE)     = rate;
            *(float*)(anim + ANIM_PRESET_A) = rate;
            *(float*)(anim + ANIM_PRESET_B) = rate;
        }
    }
#endif
}

// -----------------------------------------------------------------------------
//  No MC3_HOOK here, deliberately.
//
//  The four `jal datTimeManager::Update` sites are the per-frame callback, and
//  EVERY module that wants a frame wants them. Two modules cannot both own one
//  `jal` - the second install silently replaces the first, and one of the mods
//  just stops running. So per-frame work goes through the dispatcher, which
//  calls each module in turn, and MC3_HOOK is for hooking a SPECIFIC function
//  that only one mod is likely to care about.
//
//  A module with no MC3_HOOK is a dispatcher module: the loader puts its entry
//  in the table and the cave stub calls it once per frame.
// -----------------------------------------------------------------------------
