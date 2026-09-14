// -----------------------------------------------------------------------------
//  menu_row - one more line on the top-level menu.
//
//  WHY THIS IS C++ AND NOT ACTIONSCRIPT
//
//  The Flash Fork measured it: mcUiListMenu never calls into mcFlash at all. It
//  draws its own rows with txtFontTex::Draw and gfxPipeline::Blit2D. So the top
//  menu is not a movie with a list in it - the eight lines are eight calls in
//  mcMenuModeSelect::PopulateMenu (0x00354010):
//
//      Career, Tokyo, Arcade  (always)     Networking, Race Editor, Garage
//      Options, Quit Game     (always)     (each behind its own condition)
//
//  Which answers the question of whether the widget has room for one more: the
//  shipped game already draws seven or eight depending on those conditions. A
//  varying row count is the normal case here, not the exception.
//
//  HOOKING SOMETHING THAT HAS NO `jal`
//
//  PopulateMenu is called through a vtable - there is not one `jal 0x354010` in
//  the executable, so MC3_HOOK has nothing to patch. There is exactly ONE word
//  in the image holding its address, the vtable slot at 0x00629C6C, and that is
//  what this replaces. The module writes its own address there at init; because
//  the loader has already relocated the module, `&handler` is the real one.
//
//  THE ROW
//
//  Copied from the Career block, which is the simplest of the eight:
//
//      list = *(*(*(0x00617ADC) + 12) + 0xF0)
//      datCallback::Make(&cb, pmf, screen)         0x0042A630
//      mcUiListMenu::AddItemSimple(list, 0, 0, &cb, 2, text)   0x005881A8
//
//  The pmf is an eight-byte pointer-to-member the game keeps in .rodata, read
//  with an unaligned ldl/ldr pair because it is only 4-byte aligned. Reusing
//  Tokyo's (0x00655C44 -> function 0x00353F20) means the row does something
//  sane if it is pressed, instead of jumping into a callback we invented.
//
//  TEXT IS SIXTEEN BITS PER CHARACTER. The game's font takes u16, which
//  digital_speedometer.mod already proves in practice. An 8-bit literal here
//  would draw as garbage - the kind of failure that looks like a layout bug and
//  is not.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"

enum {
    // The vtable slot holding mcMenuModeSelect::PopulateMenu. The only word in
    // the image with that value.
    VTABLE_SLOT       = 0x00629C6C,
    POPULATE_MENU     = 0x00354010,

    ADD_ITEM_SIMPLE   = 0x005881A8,   // (list, 0, 0, &cb, kind, u16 *text)
    DAT_CALLBACK_MAKE = 0x0042A630,   // (&out, pmf, object)

    SCREEN_ROOT       = 0x00617ADC,   // -> +12 -> the menu screen
    SCREEN_OWNER      = 12,
    SCREEN_LIST       = 0xF0,         // screen -> the mcUiListMenu

    // CM_Tokyo's pointer-to-member: {0xFFFF0000, 0x00353F20}. Not 8-byte
    // aligned, hence the two-word read below.
    PMF_TOKYO         = 0x00655C44,

    ITEM_KIND         = 2,            // what all eight existing rows pass

    // --- what happens when the row is pressed -------------------------------
    //
    // The Career and Tokyo callbacks (0x00353EE8 and 0x00353F20) are the same
    // function twice over. Both clear 0x00617AE0 and go to screen 28. The whole
    // difference is one byte:
    //
    //     *(0x00617AEC) = 0   -> Career: screen 28 uses the city the player
    //                            picked, from menuobj+84
    //     *(0x00617AEC) = 1   -> Tokyo:  screen 28 calls SetCity(3), a literal
    //
    // Every reader of that byte tests it with `beq ..., zero`, so ANY non-zero
    // value takes the Tokyo route. That leaves 2 free to mean "ours" without
    // the game noticing, and without a flag of our own to keep in sync.
    GOTO_SCREEN       = 0x00346AE8,   // (root, screen id)
    SCREEN_SELECT     = 28,
    MODE_FLAG         = 0x00617AEC,   // 0 Career, 1 Tokyo, 2 ours
    MODE_AUX          = 0x00617AE0,   // both callbacks zero this
    MODE_MODCITY      = 2,

    // mcRaceConfig-ish setter: [+80] = index always, [+84] = index only when
    // it is below 3. So it already handles 3 and above generically - there is
    // no clamp to work around here.
    SET_CITY          = 0x004C7960,
    MODCITY_INDEX     = 5,

    // The second index sink, and the one that actually decided the first test.
    // The Tokyo route writes 3 into *(*(0x00619B14)) with a bare `sw` at
    // 0x003499B8 - no call to intercept, and `s1` is loaded at the top of the
    // function, before any hook of ours could run. So instead of fighting it
    // instruction by instruction, correct it AFTER: the whole function is
    // 0x00349970..0x00349A14 and has exactly one caller.
    CITY_SELECT_FN    = 0x00349970,
    CITY_HOLDER       = 0x00619B14,   // -> +0, the index the route just wrote
};

// A small trace at the tail of the report window, past the 176 bytes
// city_slot6.mod uses. Same block, different offset - they do not collide, and
// a row that fails to appear is otherwise indistinguishable from a hook that
// never ran. See MODREPORT in ../../mc3_inject.py.
#define MENU_TRACE  0x0061CFC8u
#define MENU_MAGIC  0x4D43334Eu       /* 'MC3N' */

struct menu_trace {
    mc3_u32 magic;
    mc3_u32 installed;      /* the vtable slot was written */
    mc3_u32 populated;      /* PopulateMenu came through us */
    mc3_u32 added;          /* AddItemSimple was actually reached */
    mc3_u32 list;           /* the widget we handed it to */
    mc3_u32 was;            /* what the vtable slot held before */
    mc3_u32 chosen;         /* the row was pressed */
    mc3_u32 substituted;    /* SetCity was redirected to city 5 */
    mc3_u32 corrected;      /* the second sink was put back to 5 */
    mc3_u32 holder_was;     /* what it held before we corrected it */
};

// One u16 per character.
//
// Written "Mod City", not "MOD CITY". The font renders lowercase as small caps
// - that is where the shape of "CAREER" and "TOKYO CHALLENGE" comes from - so
// all-uppercase here draws every letter full height and the row reads as
// foreign to the eight above it. Measured on screen, not guessed.
static const mc3_u16 g_label[] = {
    'M', 'o', 'd', ' ', 'C', 'i', 't', 'y', 0
};

static __attribute__((noinline)) const mc3_u16 *label()
{
    return g_label;
}

static mc3_u32 callback_address();

static inline int valid(mc3_u32 p)
{
    return p >= 0x00100000u && p < 0x02000000u && (p & 3u) == 0u;
}

// -----------------------------------------------------------------------------
//  What the row does when it is pressed.
//
//  Shaped exactly like the Tokyo callback, with 2 in the mode byte instead of
//  1. From the game's side that is Tokyo - every test is `!= 0` - so the whole
//  transition runs on rails the shipped code already uses. The only thing left
//  to change is which city screen 28 asks for, and that happens below.
// -----------------------------------------------------------------------------
extern "C" void mc3_modcity_chosen(mc3_u32 self)
{
    (void)self;
    menu_trace *const t = (menu_trace *)MENU_TRACE;
    t->chosen += 1u;

    *(volatile mc3_u32 *)MODE_AUX = 0u;
    *(volatile mc3_u8 *)MODE_FLAG = (mc3_u8)MODE_MODCITY;

    const mc3_u32 root = *(volatile mc3_u32 *)SCREEN_ROOT;
    if (valid(root))
        MC3_CALL2(void, GOTO_SCREEN, mc3_u32, int)(root, SCREEN_SELECT);
}

// -----------------------------------------------------------------------------
//  Screen 28 asks for city 3 on the Tokyo route, as a literal `addiu a1,zero,3`
//  at two call sites. These are those two: when the mode byte says the row was
//  ours, the request becomes city 5 instead.
//
//  WHAT THIS DELIBERATELY DOES NOT COVER, and it matters for reading the result
//
//  The same route also writes 3 straight into *(*(0x00619B14)) with a plain
//  `sw` at 0x003499B8 - no call, so nothing to hook, and patching the literal
//  three instructions above it would mean rewriting game code under our own
//  feet. It is left alone ON PURPOSE. There are two index sinks and this
//  intercepts one, so the boot answers a question instead of hiding it: if the
//  game lands in Tokyo anyway, that `sw` is what won, and the fix is a known
//  one-word patch. Guessing which sink matters is cheaper to measure than to
//  reason about.
// -----------------------------------------------------------------------------
extern "C" void mc3_set_city(mc3_u32 obj, int city)
{
    if (*(volatile mc3_u8 *)MODE_FLAG == (mc3_u8)MODE_MODCITY) {
        city = MODCITY_INDEX;
        ((menu_trace *)MENU_TRACE)->substituted += 1u;
    }
    MC3_CALL2(void, SET_CITY, mc3_u32, int)(obj, city);
}

// -----------------------------------------------------------------------------
//  What the vtable slot points at once this module is loaded.
// -----------------------------------------------------------------------------
extern "C" void mc3_populate_menu(mc3_u32 self)
{
    // The original writes all eight rows. Ours goes after them, so it lands at
    // the bottom of whatever the conditions produced.
    MC3_CALL1(void, POPULATE_MENU, mc3_u32)(self);

    menu_trace *const t = (menu_trace *)MENU_TRACE;
    t->populated += 1u;

    const mc3_u32 root = *(volatile mc3_u32 *)SCREEN_ROOT;
    if (!valid(root))
        return;
    const mc3_u32 screen = *(volatile mc3_u32 *)(root + SCREEN_OWNER);
    if (!valid(screen))
        return;
    const mc3_u32 list = *(volatile mc3_u32 *)(screen + SCREEN_LIST);
    if (!valid(list))
        return;
    t->list = list;

    // Our own pointer-to-member, in the shape the game's own ones have: the
    // first word is the adjustment field every one of them carries, the second
    // is the function. Reading Tokyo's told us the layout; this builds one
    // rather than borrowing it, so the row runs OUR callback.
    const mc3_u32 lo = *(const volatile mc3_u32 *)(PMF_TOKYO + 0);   /* 0xFFFF0000 */
    const unsigned long long pmf =
        ((unsigned long long)callback_address() << 32) | lo;

    // AddItemSimple reads 20 bytes out of this. 32 is room to spare.
    mc3_u32 cb[8];
    MC3_CALL3(void, DAT_CALLBACK_MAKE, void *, unsigned long long, mc3_u32)
        (cb, pmf, self);

    MC3_CALL6(void, ADD_ITEM_SIMPLE, mc3_u32, int, int, void *, int,
              const mc3_u16 *)
        (list, 0, 0, cb, ITEM_KIND, label());

    t->added += 1u;
}

// -----------------------------------------------------------------------------
//  Installation. Runs at init off a real `jal`, and all it does is redirect one
//  word - the module is already relocated by the time this runs, so taking the
//  handler's address gives the address the game will actually jump to.
// -----------------------------------------------------------------------------
static __attribute__((noinline)) mc3_u32 handler_address()
{
    return (mc3_u32)(void *)&mc3_populate_menu;
}

static __attribute__((noinline)) mc3_u32 callback_address()
{
    return (mc3_u32)(void *)&mc3_modcity_chosen;
}

// The `jal` this borrows to get called at init. NOT 0x001A0F40 - city_slot6.mod
// owns that one, and two modules cannot share a call site: the second install
// silently replaces the first and one of the mods just stops running. This is
// the call immediately before it, in the same init sequence.
#define INIT_HOOK_SITE  0x001A0F30
#define INIT_HOOK_ORIG  0x004B1480

extern "C" void mc3_menu_install()
{
    MC3_CALL(void, INIT_HOOK_ORIG)();

    menu_trace *const t = (menu_trace *)MENU_TRACE;
    for (mc3_u32 i = 0; i < sizeof(menu_trace) / 4u; ++i)
        ((mc3_u32 *)t)[i] = 0u;
    t->magic = MENU_MAGIC;

    volatile mc3_u32 *const slot = (volatile mc3_u32 *)VTABLE_SLOT;
    t->was = *slot;
    // Refuse if it is not what we expect: a vtable is shared, and writing the
    // wrong slot would redirect something else entirely.
    if (t->was != (mc3_u32)POPULATE_MENU)
        return;

    *slot = handler_address();
    t->installed = 1u;
}

MC3_HOOK(INIT_HOOK_SITE, mc3_menu_install);

// Both Tokyo-route SetCity calls in screen 28. One handler serves both - the
// same shape as toplevel_unlock.mod's two sites.
MC3_HOOK(0x00349844, mc3_set_city);
MC3_HOOK(0x003499A8, mc3_set_city);

// -----------------------------------------------------------------------------
//  The fix the first boot asked for.
//
//  Redirecting SetCity was not enough: the game still loaded Tokyo, which said
//  the OTHER sink won. This runs the whole selection function and then, if the
//  row was ours, writes the index the bare `sw` overwrote.
//
//  Correcting after the fact rather than patching the literal keeps Tokyo
//  working: the byte is only 2 when our row was the one pressed.
// -----------------------------------------------------------------------------
extern "C" void mc3_city_select()
{
    MC3_CALL(void, CITY_SELECT_FN)();

    if (*(volatile mc3_u8 *)MODE_FLAG != (mc3_u8)MODE_MODCITY)
        return;

    const mc3_u32 holder = *(volatile mc3_u32 *)CITY_HOLDER;
    if (!valid(holder))
        return;

    menu_trace *const t = (menu_trace *)MENU_TRACE;
    t->holder_was = *(volatile mc3_u32 *)holder;
    *(volatile mc3_u32 *)holder = (mc3_u32)MODCITY_INDEX;
    t->corrected += 1u;
}

MC3_HOOK(0x00348F78, mc3_city_select);
