// -----------------------------------------------------------------------------
//  debug_draw - the SaltySasha debug ELF, rebuilt as a module
//
//  WHAT THAT ELF ACTUALLY IS
//
//  Not a different build. Diffed against retail slus_213.55 it is the SAME
//  image with 435 bytes changed, plus a SECOND load segment of 2636 bytes at
//  VA 0x00715E00 - the 1.3 MB the file grew by is mostly a symbol table that is
//  never loaded. Its program headers say so outright:
//
//      LOAD  off 00000080  va 001A0000  filesz 5075060    <- identical to retail
//      LOAD  off 00642600  va 00715E00  filesz    2636    <- the new code
//
//  And of those 435 bytes, most are the cdrom0:\ -> host0: path conversion this
//  project already does for HostFS. What is left divides cleanly:
//
//    * the file and audio redirects (sceOpen, __coreRawReadFile,
//      psxCdCache::RawRead, pssOpenFile, iopManager::LoadModule, sndStream::*)
//      jump into that 2636-byte segment. That segment IS the loose-file layer,
//      which is not what anyone wants from this ELF;
//    * the debug DRAWING needs none of it. The one graphics redirect is 76
//      bytes of glue calling two functions that are already in retail.
//
//  So nothing has to be copied out of the debug ELF. Everything below is retail
//  code that was left in and simply never called.
//
//  THE DRAW, AND WHY THE HOOK SITE IS FREE
//
//  mcGame::Draw ends with `jal aiRailNetwork::Draw`, and in retail that function
//  is `{ ; }` - an empty stub, the rail rendering compiled out. The debug ELF
//  replaces that call with its own 76-byte shim, which reads:
//
//      game = *(u32*)0x00618AE0;               // the mcGame singleton
//      if (game && (race = *(u32*)(game + 4))) {
//          gfxState::SetWorld(&flt_6F61A0);    // 0x0052E090
//          mcRaceBase::DebugDrawTriggers(race);// 0x003DB6E0
//      }
//
//  mcRaceBase::DebugDrawTriggers is 28 bytes and forwards to
//  mcTriggerManager::Draw, which is 356 bytes and fully alive. Hooking the stub
//  therefore costs nothing at all: there is no original behaviour to preserve.
//
//  THE PROFILER OVERLAY
//
//  `CPU UP=%5.2f DRAW=%5.2f[%5.2f/%5.2f] VU/GS=%5.2f+%4.2f FPS=%.0f` sits at
//  0x0066FEA2 in BOTH images. gfxPipeline::EndFrame keeps six 16-byte timer
//  records, each with an enable byte, and the whole display is gated on
//  dword_6F4870. The debug ELF turns the six bytes on and forces three loads to
//  `li reg, 1`; this does the same.
//
//  THE UNLOCKS
//
//  Seven bytes read by mcPgUnlockingRulesEval::IsCityUnlocked,
//  IsVehicleUnlockedForPlayer, IsNitroUnlockedForPlayer,
//  IsSlipstreamTurboUnlockedForPlayer, IsBurnoutUnlockedForPlayer,
//  IsTwoWheelDrivingUnlockedForPlayer and mcGarage::SetStatus*. The debug ELF
//  sets all of them. Off by default here: it changes progression, which is a
//  different thing from a debug overlay, so it is a deliberate choice.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"

// Pick what you want, then rebuild.
#define WANT_TRIGGER_DRAW  1
#define WANT_PROFILER      1
#define WANT_UNLOCKS       0

enum {
    GAME_SINGLETON      = 0x00618AE0,   // dword_618AE0, read by mcGame::Draw
    GAME_RACE_OFFSET    = 0x04,

    GFX_SET_WORLD       = 0x0052E090,   // gfxState::SetWorld(Matrix44 const&)
    WORLD_MATRIX        = 0x006F61A0,   // flt_6F61A0, the matrix the shim uses
    DEBUG_DRAW_TRIGGERS = 0x003DB6E0,   // mcRaceBase::DebugDrawTriggers
    FLUSH_CACHE         = 0x00546C20,

    // The six enable bytes of gfxPipeline's timer records, 16 bytes apart.
    PROF_FLAG_0         = 0x0061C32C,
    PROF_FLAG_STRIDE    = 0x10,
    PROF_FLAG_COUNT     = 6,

    // Three loads the debug ELF replaces with `li reg, 1`. Addresses and the
    // exact encodings are from the diff, not invented:
    //   0052A0CC  lw  v1, 0x4870(v0)  -> li v1, 1   (gfxPipeline::EndFrame)
    //   0052D868  lw  v0, 0x60B0(v0)  -> li v0, 1   (ageEndDraw)
    //   0052DA58  lbu v1, 0x60B4(v0)  -> li v1, 1   (ageEndDraw)
    PROF_PATCH_A        = 0x0052A0CC,
    PROF_PATCH_B        = 0x0052D868,
    PROF_PATCH_C        = 0x0052DA58,
    LI_V0_1             = 0x24020001,
    LI_V1_1             = 0x24030001,

    UNLOCK_FLAGS        = 0x00619B1C,   // seven consecutive bytes
    UNLOCK_COUNT        = 7,
};

struct debug_draw_state {
    mc3_u32 magic;
    mc3_u32 armed;          /* the one-shot patches have been written */
    mc3_u32 draw_calls;     /* the hook fired */
    mc3_u32 trigger_draws;  /* a race was present and triggers were drawn */
};

static debug_draw_state g_state;

// One accessor for the one data base. Loose symbols make GCC share a `lui`
// across several `%lo` and mc3_mkmod refuses the build with "LO16 sem HI16" -
// and a `static` inside a function, or a local `const` array, counts as another
// base for exactly the same reason.
static __attribute__((noinline)) debug_draw_state *st(void) { return &g_state; }

static int valid_game_pointer(mc3_u32 p)
{
    return p >= 0x00100000u && p < 0x02000000u;
}

static void arm_once(void)
{
    debug_draw_state *const s = st();
    if (s->armed)
        return;
    s->armed = 1u;

#if WANT_PROFILER
    for (int i = 0; i < PROF_FLAG_COUNT; ++i)
        *(volatile mc3_u8 *)(PROF_FLAG_0 + i * PROF_FLAG_STRIDE) = 1;

    // Guard each patch on the instruction still being the one measured. A word
    // that reads as something else means the image is not what this was written
    // against, and writing it anyway would corrupt an unrelated function.
    volatile mc3_u32 *const a = (volatile mc3_u32 *)PROF_PATCH_A;
    volatile mc3_u32 *const b = (volatile mc3_u32 *)PROF_PATCH_B;
    volatile mc3_u32 *const c = (volatile mc3_u32 *)PROF_PATCH_C;
    if (*a == 0x8C434870u) *a = LI_V1_1;
    if (*b == 0x8C4260B0u) *b = LI_V0_1;
    if (*c == 0x904360B4u) *c = LI_V1_1;
#endif

#if WANT_UNLOCKS
    for (int i = 0; i < UNLOCK_COUNT; ++i)
        *(volatile mc3_u8 *)(UNLOCK_FLAGS + i) = 1;
#endif

    MC3_CALL1(void, FLUSH_CACHE, int)(0);
    MC3_CALL1(void, FLUSH_CACHE, int)(2);
}

// Stands where mcGame::Draw called aiRailNetwork::Draw. That function is an
// empty stub in retail, so nothing is lost by not calling it - which is also
// what the debug ELF concluded, since its shim does not call it either.
extern "C" void draw_debug_overlays(void)
{
    debug_draw_state *const s = st();
    ++s->draw_calls;
    arm_once();

#if WANT_TRIGGER_DRAW
    const mc3_u32 game = *(volatile mc3_u32 *)GAME_SINGLETON;
    if (!valid_game_pointer(game))
        return;
    const mc3_u32 race = *(volatile mc3_u32 *)(game + GAME_RACE_OFFSET);
    if (!valid_game_pointer(race))
        return;

    // SetWorld first, exactly as the shim does: the trigger geometry is in world
    // space and whatever transform the last draw left behind would otherwise
    // move it.
    MC3_CALL1(void, GFX_SET_WORLD, mc3_u32)(WORLD_MATRIX);
    MC3_CALL1(void, DEBUG_DRAW_TRIGGERS, mc3_u32)(race);
    ++s->trigger_draws;
#endif
}

MC3_HOOK(0x001A2EB4, draw_debug_overlays);
