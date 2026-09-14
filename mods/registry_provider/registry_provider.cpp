// -----------------------------------------------------------------------------
//  registry_provider - the other half of registry_demo
//
//  Publishes one function under an id. registry_demo.mod imports it by that id
//  and calls it, with no header shared between them and no address agreed by
//  hand. See payload/mc3_registry.h.
//
//  Hookless on purpose: the loader gives a module with no MC3_HOOK a per-frame
//  callback, so this runs whatever the player is doing. A module that exports
//  only from inside a hook publishes nothing until that hook fires, which for a
//  menu or an in-race hook can be minutes - or never, in a test that does not
//  press anything. That is a real trap and it cost a test run here: the first
//  provider was draw_distance, whose hooks are the race camera and the Camera
//  Options screen, and a headless boot touches neither.
//
//      registry_provider.mod = defer
//      registry_demo.mod = defer
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_registry.h"

struct rp_state {
    mc3_u32 calls;
    mc3_u32 published;
};
static rp_state g_state;

// One data base behind one noinline accessor - see docs/WRITING_A_MOD.md.
static __attribute__((noinline)) rp_state *st(void) { return &g_state; }

// What the other module ends up calling. Counts its own calls so the answer is
// visibly coming from HERE rather than from a constant the caller could have
// had on its own.
extern "C" mc3_u32 provider_answer(void)
{
    rp_state *const s = st();
    s->calls += 1;
    return 0xC0DE0000u + s->calls;
}

extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    rp_state *const s = st();
    if (s->published)
        return;
    if (mc3_export(MC3_ID('D', 'E', 'M', 'O'), (void *)&provider_answer))
        s->published = 1;
}
