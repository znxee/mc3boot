// -----------------------------------------------------------------------------
//  registry_demo - calling a function that lives in another mod
//
//  There is no symbol table and no dynamic linker here: every .mod is relocated
//  independently to wherever it lands, so this module cannot name a function in
//  registry_provider.mod at build time. What both sides CAN agree on is a number.
//
//      registry_provider.mod:  mc3_export(MC3_ID('D','E','M','O'), &fn);
//      this module:            fn = mc3_import(MC3_ID('D','E','M','O'));
//
//  The table itself is allocated by mc3boot before any module runs, so neither
//  side has to own it or care which loaded first. See payload/mc3_registry.h.
//
//  WHAT THIS DEMONSTRATES, and the mistake it is shaped to avoid: the import
//  happens PER FRAME, not once at load. The provider may not have run yet, and a
//  module that imported once at startup would cache a null and never look again.
//  Import where you use it, and cache the answer only once it is non-null.
//
//  Prints one line over SIO the first time the call succeeds. The answer carries
//  the provider's own call counter, so it could not have come from anywhere but
//  the other module actually running:
//
//      RGDM C0DE0001
//
//  Hookless, so the loader gives it a per-frame callback:
//
//      registry_provider.mod = defer
//      registry_demo.mod = defer
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_registry.h"
#include "../../payload/mc3_sio.h"

typedef mc3_u32 (*answer_fn)(void);

struct rd_state {
    mc3_u32 reported;
    answer_fn cached;
};
static rd_state g_state;

// One data base behind one noinline accessor - the rule every module here
// follows. See docs/WRITING_A_MOD.md on "LO16 sem HI16".
static __attribute__((noinline)) rd_state *st(void) { return &g_state; }

extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    rd_state *const s = st();

    // Look it up until it answers. The provider is a different module and may
    // not have run yet; a null here is a normal answer, not an error, so this
    // retries instead of caching the failure.
    if (!s->cached)
        s->cached = (answer_fn)mc3_import(MC3_ID('D', 'E', 'M', 'O'));
    if (!s->cached)
        return;

    if (!s->reported) {
        s->reported = 1;
        mc3_sio_mark(82, 71, 68, 77, s->cached());   // RGDM <what it answered>
    }
}
