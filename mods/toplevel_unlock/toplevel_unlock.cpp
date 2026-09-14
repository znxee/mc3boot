// -----------------------------------------------------------------------------
//  toplevel_unlock - a garage UI mod, and the first module that is not the
//  pedestrian payload.
//
//  WHAT IT DOES
//
//  The garage's top-level menu is drawn by Flash from two arrays the C++ pushes
//  into it: TopLevelNames and TopLevelStatus. mcGarage::DoTopLevelUnlocking
//  (0x3350B8) walks progress flags and writes a status per entry:
//
//      SetContextVariable(flash, "TopLevelStatus", index, 5)   unlocked
//      SetContextVariable(flash, "TopLevelStatus", index, 1)   locked
//
//  This runs after it and writes 5 over all three, so nothing in that menu is
//  locked. The original is called first and its work simply overwritten - which
//  is the cheapest correct way to do this, because the game's own conditions
//  stay intact for anything else that reads them.
//
//  WHY THIS ONE
//
//  It exercises the part of the loader that the pedestrian module does not: it
//  hooks a NAMED FUNCTION rather than taking the per-frame callback, at two
//  separate call sites, and it changes something visible on screen. If this
//  works, hooks work.
//
//  It also demonstrates the thing that made hooks worth building: a mod decides
//  where it runs. Nothing about this address is known to the loader.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"

enum {
    // mcGarage::DoTopLevelUnlocking(void)
    DO_TOPLEVEL_UNLOCKING = 0x003350B8,
    // mcFlash::SetContextVariable(char const*, int, int)
    SET_CONTEXT_VAR       = 0x00320900,
    // the literal "TopLevelStatus", already in the executable
    TOPLEVEL_STATUS       = 0x00650DA0,

    GARAGE_FLASH          = 0x18,   // mcGarage +0x18 -> the mcFlash it drives
    TOPLEVEL_ENTRIES      = 3,      // indices the game itself writes
    STATUS_UNLOCKED       = 5,
};

extern "C" void mc3_unlock_toplevel(mc3_u32 self)
{
    // The original first: it is what populates everything else about the menu.
    MC3_CALL1(void, DO_TOPLEVEL_UNLOCKING, mc3_u32)(self);

    const mc3_u32 flash = *(volatile mc3_u32 *)(self + GARAGE_FLASH);
    // Called during construction as well as during play, so the Flash object is
    // not always there yet. A null here would be a crash on the first frame.
    if (flash < 0x00100000u || flash >= 0x02000000u || (flash & 3u))
        return;

    for (int i = 0; i < TOPLEVEL_ENTRIES; ++i)
        MC3_CALL4(void, SET_CONTEXT_VAR, mc3_u32, const char *, int, int)
            (flash, (const char *)TOPLEVEL_STATUS, i, STATUS_UNLOCKED);
}

// Both call sites, because either can be the one that runs first depending on
// how the garage was entered. Hooking one and not the other would make the mod
// work only on some paths - the kind of bug that reads as "sometimes it does
// not apply".
MC3_HOOK(0x00334130, mc3_unlock_toplevel);   // mcGarage::HandleProfileBrowsing
MC3_HOOK(0x00335248, mc3_unlock_toplevel);   // mcGarage::CheckTopLevelAtZero
