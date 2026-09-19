// -----------------------------------------------------------------------------
//  race_nofe - `[boot] nofe = 1` skips the logo movies and the front end and
//  boots straight into a race; `[boot] car = <vehicle>` picks the car.
//
//  WHAT THE ALPHA DID
//
//  Inside Main_void the alpha reads PARAM_nofe and, when set, never queues the
//  front end: it asks the game-state request queue for code 6 (DoLoadRace)
//  instead. Retail has every piece of that chain, named - the request queue
//  (gGameState = dword_619958, vtable slot +0xC), DoLoadRace (0x001A76C0,
//  just ChangeState(2)), ChangeState (slot +0x20) and EnterStateGame - but not
//  the branch. Retail's Main_void reaches exactly one request unconditionally:
//
//      001a1008  lw   a0, dword_619958        ; gGameState
//      001a100c  lw   v1, 0(a0)
//      001a1010  lw   v0, 0xC(v1)             ; RequestAction
//      001a1014  jalr v0
//      001a1018  li   a1, 0x10                ; 0x10 = DoStartMovie ("rockstar")
//
//  The front end itself is only requested later, by DoEndMovie, once the
//  rockstar -> sdlogo -> mc3intro chain ends (code 0x16). Rewriting that one
//  immediate to 6 is therefore the whole of "nofe": no movie, no front end.
//  A `jalr` cannot be an MC3_HOOK site, so this hooks the `jal SetMovie` just
//  before it (0x001A0FFC) purely as a guaranteed-once anchor, chains to it,
//  and patches the immediate guarded against the measured word.
//
//  WHY THE FIRST VERSION SAT ON "LOADING" FOREVER
//
//  Seen on screen (mc3_pcsx2_shots.py): the patch worked - no movies, straight
//  to a live, animated "SAN DIEGO / ARCADE - LOADING" screen - which then never
//  finished. A savestate taken on that screen settled it:
//
//    * the main thread (tid 1, entry 0x1A0008) was RUNNING at 0x002E41A8, with
//      ra = 0x002E415C: inside mcCarType::LoadResource, right after
//      datPaging::LoadBuild returned 0, in the `while (1);` retail leaves where
//      the failed assert was;
//    * the path it had just built, still on its stack, was
//      "$/resources/vehicle/blank/blank".
//
//  The player's car was still the constructor's placeholder, "blank", which
//  has no resource directory. The front end is what normally puts a real car
//  there (mcMenuCustomRaceType::DoStartRace prepares the garage, the players
//  and the career state before its own request for code 6). So this module
//  names the car itself - in the NEXT and the CURRENT mcRaceConfig, because
//  mcGameState::EnterStateGameOrReplay reads one or the other depending on its
//  own argument - through mcCarConfig::SetVehicleTypeByName, which only copies
//  the string. It validates nothing, so a name is checked first with
//  mc::LookupCar (-1 if absent, same lookup LoadOpponents uses) and a bad one
//  falls back to CAR_DEFAULT rather than reproducing the hang.
//
//  What was ruled out on the way, so nobody chases it again: the
//  mcNetManager::WaitForSync theory. Its fast-exit field, *(dword_619D5C+16),
//  measured -1 before anything wrote it.
//
//  [boot] garage = 1
//
//  The same patch with code 0x12 (DoLoadGarage = ChangeState(5)) instead of 6:
//  what the alpha's -garage did. Retail's Main already makes the call the
//  alpha made before it, mcLocalPlayerOptions::SetToDefaults(true), at
//  0x001A0F6C. Seen on screen: the garage, in "Buy Vehicles". The likely
//  reason, not yet measured: the memory card save is loaded by the front end's
//  flow, so without it the player owns no car and the garage opens on buying
//  the first. garage wins if both keys are set.
//
//  CITY, TIME, WEATHER, RACE TYPE
//
//  race_bootargs and city_force write these every frame, but per-frame modules
//  are dispatched from mcGame's datTimeManager::Update calls, and none of those
//  run during a race load - so on this path they arrive after the world is
//  already built. The same [boot] keys are therefore applied here too, into
//  the next and current configs, before the request. Looked up with
//  payload/mc3_raceconfig.h, never the game's setters: those hang on an
//  unknown name.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_bootargs.h"
#include "../../payload/mc3_raceconfig.h"

enum {
    SITE           = 0x001A0FFC,   // jal mcRaceConfig::SetMovie(this, "rockstar")
    SET_MOVIE      = 0x004BE968,
    REQUEST_CODE   = 0x001A1018,   // li a1, <code>, right after the RequestAction call
    CODE_ORIGINAL  = 0x24050010,   // addiu a1, zero, 0x10  (DoStartMovie)
    CODE_LOADRACE  = 0x24050006,   // addiu a1, zero, 6     (DoLoadRace -> ChangeState(2))
    CODE_GARAGE    = 0x24050012,   // addiu a1, zero, 0x12  (DoLoadGarage -> ChangeState(5))
    FLUSH_CACHE    = 0x00546C20,

    CFG_CURRENT    = 0x00619B10,   // mcRaceConfig* current
    CFG_NEXT       = 0x00619B14,   // mcRaceConfig* next
    PLAYER0_CAR    = 276 + 12,     // cfg + 276 + 44*i is player i; +12 is its mcCarConfig*
    SET_CAR_NAME   = 0x004AF7D0,   // mcCarConfig::SetVehicleTypeByName(this, name)
    LOOKUP_CAR     = 0x004B2AE0,   // mc::LookupCar(name) -> index, or -1
};

struct rnf_state {
    mc3_u32 patched;
    char    car_default[12];       // "vp_350z_04", built numerically - see car_default()
};
static rnf_state g_state;
static __attribute__((noinline)) rnf_state *st(void) { return &g_state; }

static void put(char c) { *(volatile unsigned char *)0x1000F180 = (unsigned char)c; }
static void hex8(mc3_u32 v)
{
    for (int i = 28; i >= 0; i -= 4) {
        unsigned d = (v >> i) & 0xF;
        put((char)(d < 10 ? ('0' + d) : ('A' + d - 10)));
    }
}
static void tag(char a, char b, char c, char d) { put(a); put(b); put(c); put(d); put(' '); }
static void text(const char *s) { for (int i = 0; s[i] && i < 31; ++i) put(s[i]); }
static int sensato(mc3_u32 p) { return p >= 0x00100000u && p < 0x02000000u && (p & 3u) == 0u; }

// A string literal would be a second data base (the LO16/HI16 trap), so the
// fallback name lives in the one state struct and is written character codes.
static const char *car_default(void)
{
    char *d = st()->car_default;
    d[0] = 118; d[1] = 112; d[2] = 95;  d[3] = 51;  d[4] = 53;     // vp_35
    d[5] = 48;  d[6] = 122; d[7] = 95;  d[8] = 48;  d[9] = 52;     // 0z_04
    d[10] = 0;
    return d;
}

static void set_player_car(mc3_u32 cfg_ptr, const char *name)
{
    const mc3_u32 cfg = *(volatile mc3_u32 *)cfg_ptr;
    if (!sensato(cfg))
        return;
    const mc3_u32 car = *(volatile mc3_u32 *)(cfg + PLAYER0_CAR);
    if (!sensato(car))
        return;
    MC3_CALL2(void, SET_CAR_NAME, mc3_u32, const char *)(car, name);
}

// Stores the index in both configs and prints it (FFFFFFFF: not in the table,
// nothing stored - the race keeps whatever the save had).
static void apply(const char *value, int idx, mc3_u32 field, char t0, char t1, char t2, char t3)
{
    if (!value)
        return;
    if (idx >= 0) {
        mc3_rc_store(MC3_RC_NEXT, field, (mc3_u32)idx);
        mc3_rc_store(MC3_RC_CURRENT, field, (mc3_u32)idx);
    }
    tag(t0, t1, t2, t3); hex8((mc3_u32)idx); put(10);
}

static void apply_race_keys(void)
{
    const char *v = mc3_bootarg(MC3_ID('c', 'i', 't', 'y'));
    apply(v, v ? mc3_rc_lookup_city(v) : -1, MC3_RC_F_CITY, 'R', 'N', 'C', 'I');
    v = mc3_bootarg(MC3_ID('t', 'i', 'm', 'e'));
    apply(v, v ? mc3_rc_lookup(MC3_RC_T_TOD, MC3_RC_N_TOD, v) : -1,
          MC3_RC_F_TOD, 'R', 'N', 'T', 'D');
    v = mc3_bootarg(MC3_ID('w', 'e', 'a', 't'));
    apply(v, v ? mc3_rc_lookup(MC3_RC_T_WEATHER, MC3_RC_N_WEATHER, v) : -1,
          MC3_RC_F_WEATHER, 'R', 'N', 'W', 'E');
    v = mc3_bootarg(MC3_ID('r', 'a', 'c', 'e'));
    apply(v, v ? mc3_rc_lookup(MC3_RC_T_RACETYPE, MC3_RC_N_RACETYPE, v) : -1,
          MC3_RC_F_RACETYPE, 'R', 'N', 'R', 'T');
}

extern "C" void race_nofe_hook(mc3_u32 movie_config, const char *movie_name)
{
    MC3_CALL2(void, SET_MOVIE, mc3_u32, const char *)(movie_config, movie_name);

    rnf_state *const s = st();
    if (s->patched)
        return;

    const char *const nofe = mc3_bootarg(MC3_ID('n', 'o', 'f', 'e'));
    const char *const garage = mc3_bootarg(MC3_ID('g', 'a', 'r', 'a'));
    const int want_garage = garage && garage[0] != '0';
    if (!want_garage && (!nofe || nofe[0] == '0'))
        return;

    volatile mc3_u32 *const word = (volatile mc3_u32 *)REQUEST_CODE;
    if (*word != (mc3_u32)CODE_ORIGINAL) {
        tag('R', 'N', 'F', 'X'); hex8(*word); put(10);   // not the measured word: refuse
        return;
    }

    const char *car = mc3_bootarg(MC3_ID('c', 'a', 'r', 0));
    if (!car || MC3_CALL1(int, LOOKUP_CAR, const char *)(car) < 0) {
        if (car) { tag('R', 'N', 'C', 'X'); text(car); put(10); }   // unknown car: fall back
        car = car_default();
    }
    set_player_car(CFG_NEXT, car);
    set_player_car(CFG_CURRENT, car);
    tag('R', 'N', 'C', 'R'); text(car); put(10);                    // RNCR <car in use>
    apply_race_keys();

    *word = (mc3_u32)(want_garage ? CODE_GARAGE : CODE_LOADRACE);
    MC3_CALL1(void, FLUSH_CACHE, int)(0);
    MC3_CALL1(void, FLUSH_CACHE, int)(2);

    s->patched = 1u;
    tag('R', 'N', 'O', 'K'); hex8(*word); put(10);   // RNOK <the request now queued>
}

MC3_HOOK(SITE, race_nofe_hook);
