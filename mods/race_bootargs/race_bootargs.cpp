// -----------------------------------------------------------------------------
//  race_bootargs - `[boot] time / weather / racetype = <name>`, applied through
//  the game's OWN mcRaceConfig setters instead of a hand-rolled table walk.
//
//  WHAT THIS ANSWERS
//
//  The alpha's remembered flag vocabulary (nofe, car1, level, weather, tod,
//  maxopponents) does not survive as retail strings, but the ENGINE MECHANISM
//  each one would have driven was worth checking one at a time rather than
//  assumed dead across the board - city_force already proved "level"/city is
//  alive. This mod is that check for weather and time of day, and it found a
//  cleaner path than city_force's own: mcRaceConfig has real, named setters
//  for exactly these two fields, plus race type:
//
//      mcRaceConfig::SetTOD(const char*)       0x004BE508  offset +4
//      mcRaceConfig::SetWeather(const char*)   0x004BE628  offset +8
//      mcRaceConfig::SetRaceType(const char*)  0x004BE598  offset +0x18
//
//  Disassembled, all three are the same shape: walk a fixed string table with
//  strcasecmp, and on a match store the table INDEX at the given offset.
//  Anything that does not match any entry is silently ignored - exactly the
//  guard city_force's own resolve_target() re-implemented by hand for city
//  names, except here the game already does it, so this mod does not have to.
//
//  THE TABLES, read straight out of the executable's data (not guessed):
//
//      TOD (0x00619B48, 3 entries):      dawn, midnight, dusk
//      WEATHER (0x00619B58, 3 entries):  clear, cloudy, rainy
//      RACETYPE (0x00619C20, 23 entries): roam, find_hook, hookman_cruise,
//          follow, cp_unordered(_time_local/_global), cp_ordered(_time_local/
//          _global), capture_the_flag, trial, survival, tag, keepaway,
//          bomb_tag, lose_the_cops, destroy, frenzy, last_man_out, paint,
//          autocross, ordered_track
//
//  These are exactly the same three fields `mcRaceConfig::LoadInfo` fills in
//  from a race's own text file ("city"/"time"/"weather"/"racetype" keys) -
//  confirmed by decompiling it. So [boot] time/weather/racetype in the .ini
//  is doing, from three text lines instead of a rebuild, precisely what a
//  .rinf file's own "time"/"weather"/"racetype" lines already do.
//
//  WHAT IS NOT CONFIRMED
//
//  Same object as city_force (SESSAO, 0x00619B10 -> the live mcRaceConfig
//  instance), so this affects whatever that instance affects - proven for
//  city (city_force changes what the frontend loads in the background,
//  measured end to end). Whether an ACTUAL RACE later re-reads this same
//  instance for its own time/weather, or loads a fresh one from its own
//  .rinf that overwrites these fields regardless, was not traced this pass.
//  Reasserting every frame (the same reason city_force does it) covers the
//  "something copies it back" case either way; it does not cover "a race
//  never looks at this object at all". Worth confirming visually - this
//  project's headless boots have no video output to check weather/lighting
//  against.
//
//  A FOURTH FIELD, "ForcedCar", exists in the SAME text format LoadInfo
//  reads - measured by disassembly to be read into a buffer and then never
//  used again in that function. Vestigial, the same shape as ResponseFile in
//  mc3_native_args.h: the parser still accepts the key so old data files do
//  not fail to load, but nothing in retail consumes the value. That is why
//  there is no [boot] car here - forcing a car turned out to need the
//  garage's own live carousel state (mcGarage::CheckShouldLoadNewCar), not a
//  simple field, and is being left to whichever mod takes that on directly
//  rather than bolted on here as a guess.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_bootargs.h"

enum {
    SESSAO       = 0x00619B10,   // pointer to the session mcRaceConfig (city_force's own)
    SET_TOD      = 0x004BE508,
    SET_WEATHER  = 0x004BE628,
    SET_RACETYPE = 0x004BE598,
};

struct rba_state {
    mc3_u32 tod_hits;        // how many times SetTOD actually ran (nonzero once)
    mc3_u32 weather_hits;
    mc3_u32 racetype_hits;
};
static rba_state g_state;
static __attribute__((noinline)) rba_state *st(void) { return &g_state; }

static void put(char c) { *(volatile unsigned char *)0x1000F180 = (unsigned char)c; }
static void hex8(mc3_u32 v)
{
    for (int i = 28; i >= 0; i -= 4) {
        unsigned d = (v >> i) & 0xF;
        put((char)(d < 10 ? ('0' + d) : ('A' + d - 10)));
    }
}
static void tag(char a, char b, char c, char d) { put(a); put(b); put(c); put(d); put(' '); }
static int sensato(mc3_u32 p) { return p >= 0x00100000u && p < 0x02000000u && (p & 3u) == 0u; }

// Calls a SetXxx(this, value) setter and, the first time only, reports what
// the field actually holds afterward - the game's own validation already
// rejects an unrecognised name, so reading the field back is how this mod
// tells "applied" apart from "the .ini named something the table does not
// have", without duplicating the game's string table itself.
static void apply_once(mc3_u32 *hits, mc3_u32 ptr, mc3_u32 field_off,
                        mc3_u32 setter, const char *value,
                        char t0, char t1, char t2, char t3)
{
    MC3_CALL2(void, setter, mc3_u32, const char *)(ptr, value);
    if (*hits == 0) {
        *hits = 1u;
        tag(t0, t1, t2, t3);
        hex8(*(volatile mc3_u32 *)(ptr + field_off));
        put(10);
    }
}

extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    rba_state *const s = st();

    const mc3_u32 ptr = *(volatile mc3_u32 *)SESSAO;
    if (!sensato(ptr))
        return;                                  // not constructed yet

    const char *const tod = mc3_bootarg(MC3_ID('t', 'i', 'm', 'e'));
    if (tod)
        apply_once(&s->tod_hits, ptr, 4, SET_TOD, tod, 'R', 'B', 'T', 'D');       // RBTD <tod index now in the field>

    const char *const weather = mc3_bootarg(MC3_ID('w', 'e', 'a', 't'));
    if (weather)
        apply_once(&s->weather_hits, ptr, 8, SET_WEATHER, weather, 'R', 'B', 'W', 'E'); // RBWE <weather index>

    const char *const racetype = mc3_bootarg(MC3_ID('r', 'a', 'c', 'e'));
    if (racetype)
        apply_once(&s->racetype_hits, ptr, 0x18, SET_RACETYPE, racetype, 'R', 'B', 'R', 'T'); // RBRT <racetype index>
}
