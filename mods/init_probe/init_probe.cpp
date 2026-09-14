// -----------------------------------------------------------------------------
//  init_probe - did the early pass actually make the three init mods work?
//
//  Its first job was to measure whether a module can be LOADED from inside
//  Main's init: at 001A0F28 the game's allocator returned real blocks and the
//  raw stream API opened core.mod and reported its exact size, so yes. That
//  answer is what the early pass in core.cpp is built on.
//
//  Its second job, this one, is to check the RESULT rather than the mechanism.
//  Counters saying "5 modules loaded, 12 hooks routed" are what fooled me once
//  already - they prove the loader did its half, not that the mods ran. So this
//  hooks a call AFTER all three install sites and reads back what each mod is
//  supposed to have written into the report window:
//
//      001A0F28  ioFFWheel::InitClass        <- core_early loads shim modules
//      001A0F30  mc::AllocateCarData             menu_row installs here
//      001A0F38  mc::AllocateAudSfx              proper_widescreen_menu
//      001A0F40  mc::AllocateCityData            city_slot6
//      001A0F5C  mcControlConfigMgr::Load    <- this probe
//
//      MODREPORT+0    'MC3Y'  city_slot6
//      MODREPORT+200  'MC3N'  menu_row
//      MODREPORT+240  'MC3W'  proper_widescreen(.mod + _menu.mod)
//
//  A magic that is present means that mod's boot-time hook ran with its body in
//  place. A magic that is absent means it did not, whatever the counters said.
//
//  Runs at `= 1`, and takes no site any other mod wants.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_sio.h"

enum {
    SITE      = 0x001A0F5C,   // mcControlConfigMgr::Load, after all three
    ORIGINAL  = 0x004BAC28,   // what that jal pointed at, decoded from the ELF
    MODREPORT = 0x0061CF00,
};

// The probe takes a0 because the site does: 001A0F58 is `move $a0, $v0`, so the
// manager pointer is already in place when the jal is reached. Forwarding it is
// the difference between calling Load and calling Load with garbage.
extern "C" void init_probe_hook(mc3_u32 a0)
{
    MC3_CALL1(void, ORIGINAL, mc3_u32)(a0);

    // Each magic, straight out of the report window. Printed as the value so a
    // zero is as readable as a hit - "SLT6 4D433359" is city_slot6 present,
    // "SLT6 00000000" is city_slot6 never having run.
    mc3_sio_mark(83, 76, 84, 54,                    // SLT6 <- city_slot6 'MC3Y'
                 *(volatile mc3_u32 *)(MODREPORT + 0));
    mc3_sio_mark(77, 82, 79, 87,                    // MROW <- menu_row 'MC3N'
                 *(volatile mc3_u32 *)(MODREPORT + 200));
    mc3_sio_mark(80, 87, 83, 77,                    // PWSM <- widescreen 'MC3W'
                 *(volatile mc3_u32 *)(MODREPORT + 240));
}

MC3_HOOK(SITE, init_probe_hook);
