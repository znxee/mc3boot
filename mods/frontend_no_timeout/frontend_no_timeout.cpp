#include "../../payload/mc3_mod.h"

// SLUS-21355: mcMenuTitleScreen::Update, automatic 30-second attract movie.
// Keep the delay-slot timer store and the normal base-menu update intact.
extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    volatile mc3_u32 *const code = (volatile mc3_u32 *)0x003634CCu;
    if (code[2] == 0x10000025u) return; // Already applied; safe for per-frame defer.
    if (code[0] != 0x46011034u || code[1] != 0xE6210130u ||
        code[2] != 0x45000025u || code[3] != 0xE620012Cu ||
        code[4] != 0x0C0C893Eu) return; // Wrong executable or conflicting patch.

    code[2] = 0x10000025u; // b 0x0036356C (formerly bc1f).
    MC3_CALL1(void, 0x00546C20u, int)(0);
    MC3_CALL1(void, 0x00546C20u, int)(2);
}
