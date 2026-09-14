// -----------------------------------------------------------------------------
//  draw_distance - a working Draw Distance row in Video Options
//
//  THE GAME ALREADY HAS THIS OPTION. IT IS JUST NOT WIRED TO ANYTHING.
//
//  mcGlobalGameOptions keeps a draw-distance slot at +0x50 and the setter at
//  0x004BAF60 writes it, with SetToDefaults passing 500. But the only callers of
//  that setter are SetToDefaults and Load - no menu row writes it, and nothing
//  anywhere reads it. Fork Alpha 2004 measured the same dead pair in the alpha
//  build and warned not to bother hooking it expecting something to consume the
//  value; confirmed here in retail, where neither function is even named.
//
//  So the slot is free storage that the game itself saves to and loads from the
//  memory card. This module uses it for the level index, which is why the
//  setting survives a reboot without any file of its own.
//
//  WHAT ACTUALLY MOVES THE DRAW DISTANCE
//
//  Two levers, both measured, and they behave differently:
//
//    LOD - mcCullableType::GetLODDist is virtual and the base implementation is
//    two instructions at 0x005BB7C0: `lui $at, 0x42C8 / mtc1 $at, $f0`, which is
//    `return 100.0f`. mcCullable::CalcLOD 0x0024A3F0 squares that against the
//    camera distance and sets the cullable's +0x08 flag. Because the constant
//    lives in the LUI's immediate, one word changes the LOD distance for
//    everything, and it takes effect on the very next frame.
//
//    TRAFFIC - three consecutive floats at 0x006157D0, 200.0 / 75.0 / 60.0:
//    aiAmbientVehicle::ms_crDefAmbVehDrawDist, ms_crDefAmbientNoPartDrawDist and
//    ms_crDefAmbientShadowMaxDrawDist. These are DEFAULTS that get copied into
//    live fields, so they are written here for completeness but should not be
//    expected to change anything already spawned - traffic picks them up as it
//    cycles.
//
//    MIPMAP BIAS - a global float at 0x0061C428, shipped at -3.5, that
//    gfxTexture::InitDma feeds to SetMipmapBias for every texture it builds. It
//    becomes the GS's K, a straight offset on the hardware LOD, so raising it
//    drops texture resolution nearer the player. See MIP_BIAS below.
//
//  Only floats whose low 16 bits are zero can ride in a LUI immediate. Every
//  level below is chosen to satisfy that: 50, 100, 200, 400 and 800 are
//  0x42480000, 0x42C80000, 0x43480000, 0x43C80000 and 0x44480000.
//
//  WHICH TEXTURES THE BIAS REACHES, and how that was established
//
//  Only the ones the game builds at runtime. There are two ways a gfxTexture
//  comes into being and they do NOT agree:
//
//    gfxTexture::gfxTexture(gfxMipChain*) 0x0052B5C0 calls InitDma, which reads
//    the globals and calls SetMipmapBias. This is the path that responds.
//
//    gfxTexture::gfxTexture(datResource&) 0x0052C840 - the load-in-place path a
//    .pck takes - only relocates +0x84, +0x90 and +0x94 by the resource delta
//    and stamps its 0x186A1 sentinel at +0x98. It never touches +0x20, so a
//    texture baked into a .pck keeps whatever TEX1 the authoring machine wrote.
//
//  Which side the map falls on was measured rather than assumed. InitDma stamps
//  0xCAFEBABE at texture+0x6C, so a baked gfxTexture is findable by that marker
//  and its TEX1 readable at base+0x20. Across all 8544 .pck: every city file in
//  resources/city has ZERO markers, while flash/bg_main.pck has 232 and the
//  other frontend backgrounds 232-250 each - all reading K = -3.5, MXL = 0.
//
//  So the split is the one this option wants: the world is built at runtime and
//  follows the bias, the frontend is baked and does not. What it also means is
//  that the bias applies AT TEXTURE CONSTRUCTION - changing the row re-biases
//  the next city or car that loads, not what is already on screen.
//
//  THE ROW
//
//  PopulateMenu is reached through a vtable - there is no `jal` to patch - and
//  exactly ONE word in the image holds each screen's. Same technique
//  menu_row.mod uses for the top-level menu, and the same guard: the slot must
//  still contain the expected address before it is touched. The original is
//  called first, so the stock rows stay.
//
//  It goes on Game Options, NOT Video Options. mcMenuVideo exists but nothing
//  in the Options list navigates to it - see the comment on VTABLE_SLOT.
//
//  Pressing the row runs a callback of ours, which is the part with no
//  precedent here. datCallback takes an eight-byte pointer-to-member BY VALUE in
//  a1: the game's own, at 0x00657FC4, reads {0xFFFF0000, 0x0033A2C0} - the first
//  word is the not-virtual marker and the second is the function. Forging one is
//  therefore just `((u64)func << 32) | 0xFFFF0000`.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_registry.h"

enum {
    // WHICH SCREEN THE ROW GOES ON.
    //
    // Not Video Options, even though that is where a draw distance belongs:
    // measured in game, the Options list has Controller Mappings, Game Options,
    // HUD Options, Audio Options, Camera Options, Cheat Codes and Credits, and
    // NOTHING navigates to mcMenuVideo. Its screen exists in code and is
    // unreachable, so a row added there is invisible.
    //
    // Each of these has exactly ONE word in the image holding its PopulateMenu,
    // so swapping the pair below moves the row and nothing else:
    //
    //     mcMenuGeneral  slot 0x00628A5C  fn 0x003488E0   <- Game Options
    //     mcMenuHud      slot 0x0062993C  fn 0x00352528   <- HUD Options
    //     mcMenuAudio    slot 0x0062C1BC  fn 0x003843E0   <- Audio Options
    //     mcMenuVideo    slot 0x0062AD6C  fn 0x00364358   <- unreachable
    VTABLE_SLOT      = 0x00628A5C,
    POPULATE_MENU    = 0x003488E0,

    MENU_BRAIN       = 0x00617ADC,   // dword_617ADC; +12 is the shell
    SHELL_LIST       = 0xF0,         // shell+240 is the mcUiListMenu
    SHELL_SCREEN     = 0x0C,         // shell+12 is what callbacks bind to

    ADD_ITEM_SIMPLE  = 0x005881A8,   // (list, u32, u32, cb, int, u16 const*)
    DAT_CALLBACK     = 0x0042A630,   // (cb, pmf by value in a1, base)
    PMF_NOT_VIRTUAL  = 0xFFFF0000u,

    // mcGlobalGameOptions singleton, from the ctor call site at 0x004B758C.
    OPTIONS          = 0x006E0328,
    OPT_DRAWDIST     = 0x50,         // the dead slot; default 500

    // mcCullableType::GetLODDist, base implementation. The float is the LUI's
    // immediate in the FIRST word.
    GETLODDIST       = 0x005BB7C0,
    LUI_AT           = 0x3C010000u,  // lui $at, <imm>

    TRAFFIC_DEFAULTS = 0x006157D0,   // veh 200.0, noPart 75.0, shadow 60.0
    FLUSH_CACHE      = 0x00546C20,

    // Where the module gets itself run. A per-frame callback is NOT enough:
    // measured on this setup, hooked modules run and frame modules do not, so
    // the install rides an init call instead. 0x001A0F30 is menu_row's and
    // 0x001A0F40 is city_slot6's; this takes the one between them and calls the
    // original first, or the game loses its sound effects.
    // The far clip. gfxViewport::Perspective(this, fov, aspect, near, far)
    // takes its four floats in f12..f15, and `far` is f15 - established from
    // the arithmetic, not assumed: the function writes near to viewport+316,
    // far to +320, and builds -2*far*near/(far-near) for the projection matrix.
    PERSPECTIVE      = 0x005311F8,

    // WHERE THE ROW IS ADDED, and why it is not a vtable redirect any more.
    //
    // Two install sites were tried and measured, and both failed for their own
    // reason: an init call (0x001A0F38) is already past by the time a module
    // late in mc3boot.ini is loaded, and camBaseCS::UpdateView (0x0031EEAC)
    // never runs while the frontend is up, so the row could not appear on the
    // one screen that needed it. mcLedMgr::UpdateAll sits behind the same
    // in-game branch and failed the same way.
    //
    // So the row is not installed at all now - it is added from INSIDE the
    // function that builds the screen. 0x00348CCC is the LAST call in
    // mcMenuGeneral::PopulateMenu, a SetTextElement finishing the last stock
    // row, which means the screen is complete and the list is right there. No
    // timing, no vtable, no frontend-versus-ingame question.
    ROW_SITE         = 0x00348CCC,
    SET_TEXT_ELEMENT = 0x00588540,   // mcUiListMenu::SetTextElement
    // Hooked at camBaseCS::UpdateView's call only. The other sixteen callers
    // are the rear-view mirror, the hud arrow and the glow pass, and scaling
    // their far plane would distort them for nothing.
    PERSP_SITE       = 0x0031EEAC,

    // MIPMAPPING. The game does not pick mip levels by distance itself - the
    // GS does, in hardware, with LOD = (log2(1/|Q|) << L) + K. The engine only
    // fills in L and K, once per texture, in gfxTexture::SetMipmapBias
    // 0x0052BD48, which rewrites two fields of the texture's cached TEX1 qword
    // at texture+0x20:
    //
    //     K = (int)(bias * 16.0) & 0xFFF;             // bits 32-43, 4 frac
    //     tex1 = (tex1 & 0xFFFFF000FFE7FFFF) | (L << 19) | ((u64)K << 32);
    //
    // K is a straight offset on the LOD, so RAISING it makes the hardware
    // reach for a smaller mip closer to the player - which is the knob asked
    // for. Both arguments come from these globals, written once at boot by
    // gfxPipeline::Begin 0x005290F0 (one caller, and it is not the per-frame
    // BeginFrame 0x005295F0) and read by gfxTexture::InitDma 0x0052B618 for
    // every texture it builds. So no hook is needed and none is spent: the
    // whole feature is a word written into the image.
    MIP_L            = 0x0061C424,   // int   L, TEX1 bits 19-20; ships 0
    MIP_BIAS         = 0x0061C428,   // float bias -> K;          ships -3.5
    MIP_ENABLE       = 0x0061C42C,   // byte, the on/off flag;    ships 1

    LEVEL_COUNT      = 5,
    LEVEL_DEFAULT    = 1,            // the 100.0 the game shipped with
};

struct dd_state {
    mc3_u32 magic;
    mc3_u32 level;
    mc3_u32 taken;          /* the vtable slot was redirected */
    mc3_u32 populate_calls;
    mc3_u32 cycle_calls;
    mc3_u32 applied;
    mc3_u32 pmf_lo;         /* the forged pointer-to-member, as two words */
    mc3_u32 pmf_hi;
    mc3_u32 cb[8];          /* a datCallback, built in place */
    mc3_u16 label[32];      /* the row text, UCS-2 */
};

static dd_state g_state;

// One accessor for the one data base. Loose symbols make GCC share a `lui`
// across several `%lo` and mc3_mkmod refuses the build with "LO16 sem HI16";
// a `static` inside a function and a local `const` array each count as another
// base for the same reason, so neither appears below.
static __attribute__((noinline)) dd_state *st(void) { return &g_state; }

// 50, 100, 200, 400, 800 - the five that fit a LUI immediate. Returned by
// computation rather than from a table, because a table would be a data base.
static mc3_u32 level_distance(mc3_u32 level)
{
    if (level == 0) return 50u;
    if (level == 1) return 100u;
    if (level == 2) return 200u;
    if (level == 3) return 400u;
    return 800u;
}

// The float bits of the distances above. All have a zero low half, which is the
// whole reason these five values were chosen.
static mc3_u32 level_float_bits(mc3_u32 level)
{
    if (level == 0) return 0x42480000u;   /* 50  */
    if (level == 1) return 0x42C80000u;   /* 100 */
    if (level == 2) return 0x43480000u;   /* 200 */
    if (level == 3) return 0x43C80000u;   /* 400 */
    return 0x44480000u;                   /* 800 */
}

// ONE hook, and it is the camera's, not an init call. Two measurements forced
// this shape:
//
//   * the module's install hook at 0x001A0F38 WAS patched in and still never
//     ran - with this module loading last, Main's init sequence had already
//     gone past that call by the time the loader got to it. An init-time hook
//     only works for a module early in mc3boot.ini, and being early is what
//     displaced proper_widescreen_menu;
//   * the second hook was refused outright. The loader reported 21 hooks
//     installed and 0x0031EEAC was left untouched, so there is a ceiling and
//     this module does not get to spend two.
//
// camBaseCS::UpdateView runs every frame, forever, so it has neither problem.
// The trampoline parks the four float arguments and `this` across the C call -
// C is free to clobber f0-f19 - then applies the scale and tail-jumps into
// gfxViewport::Perspective with everything as the game left it.
extern "C" void install_once(void);
extern "C" void perspective_scaled_far(void);
__asm__(
    ".set noreorder\n"
    ".set noat\n"
    ".text\n"
    ".align 3\n"
    ".globl perspective_scaled_far\n"
    "perspective_scaled_far:\n"
    "  addiu $sp, $sp, -48\n"
    "  sd    $ra, 0($sp)\n"
    "  swc1  $f12, 8($sp)\n"
    "  swc1  $f13, 12($sp)\n"
    "  swc1  $f14, 16($sp)\n"
    "  swc1  $f15, 20($sp)\n"
    "  sw    $a0, 24($sp)\n"
    "  jal   install_once\n"
    "  nop\n"
    "  lw    $a0, 24($sp)\n"
    "  lwc1  $f12, 8($sp)\n"
    "  lwc1  $f13, 12($sp)\n"
    "  lwc1  $f14, 16($sp)\n"
    "  lwc1  $f15, 20($sp)\n"
    "  ld    $ra, 0($sp)\n"
    "  addiu $sp, $sp, 48\n"
    // The scale, as a LUI immediate the C side rewrites - same trick as
    // GetLODDist, and why the factors are 0.5/1/2/4/8: their float bits are
    // 0x3F000000, 0x3F800000, 0x40000000, 0x40800000 and 0x41000000, all with a
    // zero low half. This instruction is SCALE_INSN below.
    "  lui   $at, 0x3F80\n"
    "  mtc1  $at, $f0\n"
    "  mul.s $f15, $f15, $f0\n"
    "  lui   $t9, 0x0053\n"
    "  ori   $t9, $t9, 0x11F8\n"   // gfxViewport::Perspective
    "  jr    $t9\n"
    "  nop\n"
    ".set at\n"
    ".set reorder\n");

// The scale instruction's word index inside the trampoline. VERIFIED against
// the built object rather than counted by eye - counting gave 17 and the
// disassembly puts `lui at,0x3f80` at offset 0x40, which is word 16. If the
// assembly above changes, disassemble again; writing the wrong word here would
// corrupt the trampoline instead of scaling anything.
enum { SCALE_INSN_WORD = 16 };

// Behind its own accessor for the same reason g_state is: taking a function's
// address is a HI16/LO16 pair, and GCC will share one `lui` between this and
// the state base, which mc3_mkmod refuses - measured, at +B4.
static __attribute__((noinline)) volatile mc3_u32 *persp_code(void)
{
    return (volatile mc3_u32 *)&perspective_scaled_far;
}

static mc3_u32 level_scale_bits(mc3_u32 level)
{
    if (level == 0) return 0x3F000000u;   /* 0.5 */
    if (level == 1) return 0x3F800000u;   /* 1.0 */
    if (level == 2) return 0x40000000u;   /* 2.0 */
    if (level == 3) return 0x40800000u;   /* 4.0 */
    return 0x41000000u;                   /* 8.0 */
}

// The mipmap bias per level, as raw float bits. Written as bits rather than as
// a float literal for the usual reason: a literal lands in .rodata and becomes
// a second data base, which the packer refuses.
//
// Centred on the -3.5 the game ships, so level 1 is stock and the row still
// means "the game as it was" in the middle. The direction is the one that pays
// for itself: a longer draw distance puts more textured surface on screen at
// once, so the bias rises and the GS spends less texture bandwidth per pixel; a
// short draw distance leaves bandwidth over, so the bias drops and near
// surfaces come back sharper than stock. End to end that is four LOD levels.
static mc3_u32 level_mip_bits(mc3_u32 level)
{
    if (level == 0) return 0xC0900000u;   /* -4.5  sharper than the game ships */
    if (level == 1) return 0xC0600000u;   /* -3.5  stock */
    if (level == 2) return 0xC0200000u;   /* -2.5 */
    if (level == 3) return 0xBFC00000u;   /* -1.5 */
    return 0xBF000000u;                   /* -0.5  lowest resolution */
}

static int valid_game_pointer(mc3_u32 p)
{
    return p >= 0x00100000u && p < 0x02000000u;
}

static mc3_u32 read_level(void)
{
    const mc3_u32 v = *(volatile mc3_u32 *)(OPTIONS + OPT_DRAWDIST);
    // The stock default is 500, and a memory card written before this module
    // existed will hold it. Anything outside the level range means "never set
    // by us", so fall back rather than index off the end.
    return v < LEVEL_COUNT ? v : (mc3_u32)LEVEL_DEFAULT;
}

static void apply(mc3_u32 level)
{
    dd_state *const s = st();
    const mc3_u32 bits = level_float_bits(level);

    // LOD: rewrite the LUI immediate in GetLODDist. Guarded on the instruction
    // still being a `lui $at, imm` - a word that reads as anything else means
    // this is not the image the addresses were measured against.
    volatile mc3_u32 *const lod = (volatile mc3_u32 *)GETLODDIST;
    if ((*lod & 0xFFFF0000u) == LUI_AT)
        *lod = LUI_AT | (bits >> 16);

    // The far clip, which is what actually decides how far you can SEE. The LOD
    // patch above only moves where models swap detail - CalcLOD stores a
    // boolean, not a range - so on its own it changes nothing visible, which is
    // exactly what testing showed.
    volatile mc3_u32 *const persp = persp_code() + SCALE_INSN_WORD;
    if ((*persp & 0xFFFF0000u) == LUI_AT)
        *persp = LUI_AT | (level_scale_bits(level) >> 16);

    // Traffic, scaled from the shipped 200 / 75 / 60 by the same ratio the LOD
    // moved. These are defaults copied into live fields, so they take hold as
    // traffic cycles rather than at once.
    //
    // Written as float BITS, and the arithmetic is an exponent increment rather
    // than a multiply. Each level doubles the distance, so each level adds one
    // to the exponent, which is 0x00800000 in the bit pattern - exact, and it
    // keeps every constant an integer. That matters: expressed as float
    // literals this block built for a while and then stopped, because adding
    // one more store below raised register pressure enough that GCC moved three
    // of the fifteen constants into .rodata and emitted their `lui %hi` pairs
    // out of order with the `%lo` loads. mc3_mkmod reported "LO16 sem HI16 em
    // +164" and the offending instruction was a .rodata float load, not
    // anything to do with the line that was added.
    const mc3_u32 step = level * 0x00800000u;
    volatile mc3_u32 *const tr = (volatile mc3_u32 *)TRAFFIC_DEFAULTS;
    tr[0] = 0x42C80000u + step;   /* veh     100 .. 1600 */
    tr[1] = 0x42160000u + step;   /* noPart   37.5 ..  600 */
    tr[2] = 0x41F00000u + step;   /* shadow   30  ..  480 */
    const mc3_u32 d = level_distance(level);

    // Mipmapping, on the same level. One word, no hook - gfxTexture::InitDma
    // reads this global for every texture it builds. L is deliberately left at
    // the shipped 0: it multiplies the LOD SLOPE rather than offsetting it, so
    // moving it would change how fast mips swap with distance on top of the
    // offset and make the two effects impossible to tell apart in a screenshot.
    // MIP_ENABLE is left at 1 for the same reason - it is the on/off switch,
    // not a dial, and turning it off would disable mipmapping outright.
    *(volatile mc3_u32 *)MIP_BIAS = level_mip_bits(level);

    MC3_CALL1(void, FLUSH_CACHE, int)(0);
    MC3_CALL1(void, FLUSH_CACHE, int)(2);
    s->applied = d;
}

// Published so another module can ask what the player chose without knowing
// anything about this one - no header shared, no address agreed by hand, just
// the id. See payload/mc3_registry.h.
extern "C" mc3_u32 dd_current_distance(void)
{
    return level_distance(read_level());
}

// Publishing is deliberately OUTSIDE the one-shot guard below, and that is a
// bug fixed rather than a style choice: the export first went inside it, and
// row_tail sets the same guard, so visiting any menu before a race meant the
// export never ran and every importer saw nothing. Re-exporting is harmless -
// the registry replaces an entry with the same id.
static void publish(void)
{
    mc3_export(MC3_ID('D', 'D', 'S', 'T'), (void *)&dd_current_distance);
}

// "Draw Dist: <n>" in UCS-2, character by character. The game's font takes
// sixteen bits per character - an 8-bit literal draws as garbage - and building
// it numerically keeps the module at one data base.
static void build_label(mc3_u32 level)
{
    dd_state *const s = st();
    int n = 0;
    s->label[n++] = 68;  s->label[n++] = 114; s->label[n++] = 97;
    s->label[n++] = 119; s->label[n++] = 32;  s->label[n++] = 68;
    s->label[n++] = 105; s->label[n++] = 115; s->label[n++] = 116;
    s->label[n++] = 58;  s->label[n++] = 32;

    mc3_u32 v = level_distance(level);
    mc3_u32 div = 100u;
    int seen = 0;
    while (div) {
        const mc3_u32 digit = (v / div) % 10u;
        if (digit || seen || div == 1u) {
            s->label[n++] = (mc3_u16)(48u + digit);
            seen = 1;
        }
        div /= 10u;
    }
    s->label[n] = 0;
}

extern "C" void cycle_draw_distance(mc3_u32 bound);

static void add_row(mc3_u32 shell)
{
    dd_state *const s = st();
    const mc3_u32 list = *(volatile mc3_u32 *)(shell + SHELL_LIST);
    if (!valid_game_pointer(list))
        return;

    build_label(read_level());

    // The pointer-to-member, forged: low word is the not-virtual marker, high
    // word is our function. datCallback takes it BY VALUE in a1 as eight bytes
    // - the game's own at 0x00657FC4 reads {0xFFFF0000, 0x0033A2C0} - so this
    // is a row that runs code of ours, not one that merely navigates. The
    // module is already relocated, so &f is the real address.
    s->pmf_lo = PMF_NOT_VIRTUAL;
    s->pmf_hi = (mc3_u32)&cycle_draw_distance;

    typedef void (*cb_fn)(mc3_u32, unsigned long long, mc3_u32);
    const unsigned long long pmf =
        ((unsigned long long)s->pmf_hi << 32) | (unsigned long long)s->pmf_lo;
    ((cb_fn)DAT_CALLBACK)((mc3_u32)s->cb, pmf,
                          *(volatile mc3_u32 *)(shell + SHELL_SCREEN));

    typedef void (*add_fn)(mc3_u32, mc3_u32, mc3_u32, mc3_u32, int,
                           const mc3_u16 *);
    ((add_fn)ADD_ITEM_SIMPLE)(list, 0u, 0u, (mc3_u32)s->cb, 2, s->label);
}

// Stands on the last SetTextElement of mcMenuGeneral::PopulateMenu: forward it
// untouched, then put our row at the end of the list the game has just built.
extern "C" void row_tail(mc3_u32 list, mc3_u32 item, int which,
                         const mc3_u16 *text)
{
    dd_state *const s = st();
    typedef void (*set_fn)(mc3_u32, mc3_u32, int, const mc3_u16 *);
    ((set_fn)SET_TEXT_ELEMENT)(list, item, which, text);

    ++s->populate_calls;
    const mc3_u32 brain = *(volatile mc3_u32 *)MENU_BRAIN;
    if (!valid_game_pointer(brain))
        return;
    const mc3_u32 shell = *(volatile mc3_u32 *)(brain + 12);
    if (!valid_game_pointer(shell))
        return;

    // First time through, put the saved level into effect. Doing it here as
    // well as from the camera trampoline means it applies whether the player
    // reaches a menu or a race first.
    if (s->magic != 0x4D433344u) {
        s->magic = 0x4D433344u;
        s->level = read_level();
        apply(s->level);
    }
    publish();
    add_row(shell);
}

// What the row runs when it is pressed. Reached through the forged
// pointer-to-member, so it arrives with the bound object in a0 - unused here.
extern "C" void cycle_draw_distance(mc3_u32 /*bound*/)
{
    dd_state *const s = st();
    ++s->cycle_calls;

    s->level = (read_level() + 1u) % LEVEL_COUNT;
    // Store through the game's own setter so its "options changed" bookkeeping
    // runs and the value goes to the memory card with everything else.
    MC3_CALL2(void, 0x004BAF60, mc3_u32, mc3_u32)(OPTIONS, s->level);
    apply(s->level);
}

// Called from the camera trampoline, so a race that starts before any menu is
// visited still gets the saved level.
extern "C" void install_once(void)
{
    dd_state *const s = st();
    if (s->magic != 0x4D433344u) {
        s->magic = 0x4D433344u;
        s->level = read_level();
        apply(s->level);
    }
    publish();
}

MC3_HOOK(ROW_SITE, row_tail);
MC3_HOOK(PERSP_SITE, perspective_scaled_far);
