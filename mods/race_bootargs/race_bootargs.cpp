// -----------------------------------------------------------------------------
//  race_bootargs - `[boot] time / weather / racetype = <name>` written into the
//  current mcRaceConfig, every frame, for whatever reads it next.
//
//  WHERE THE VALUES LAND
//
//  mcRaceConfig::LoadInfo (decompiled) reads exactly these fields from a race's
//  own text file, through three setters:
//
//      mcRaceConfig::SetTOD(const char*)       0x004BE508  offset +4
//      mcRaceConfig::SetWeather(const char*)   0x004BE628  offset +8
//      mcRaceConfig::SetRaceType(const char*)  0x004BE598  offset +0x18
//
//  Each walks a fixed string table with strcasecmp and stores the INDEX.
//  Tables (read out of the executable): time = dawn/midnight/dusk, weather =
//  clear/cloudy/rainy, race type = 23 entries from roam to ordered_track.
//
//  WHY THIS DOES NOT CALL THEM
//
//  An earlier version did, on the belief that an unknown name is silently
//  ignored. It is not: after the last table entry every one of those setters
//  falls into `while (1);` - retail's compiled-out assert. A typo in the .ini
//  would have hung the game. payload/mc3_raceconfig.h does the same walk,
//  answers -1 instead, and this module only stores an index it found.
//
//  TIMING - WHEN THIS ARRIVES TOO LATE
//
//  Per-frame modules are dispatched from the game's datTimeManager::Update
//  calls in mcGame, none of which run during a race load. So when a race is
//  loaded before the first frame - race_nofe's boot straight into a race -
//  these values arrive after the world they describe is already up. race_nofe
//  applies the same keys itself, before it requests the race, for that case.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_bootargs.h"
#include "../../payload/mc3_raceconfig.h"

struct rba_state {
    mc3_u32 reported;     // bit per field: its first outcome has been printed
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

// Looks the value up, stores it, and prints the outcome once per field:
// the index, or FFFFFFFF when the name is not in the table (nothing stored).
static void apply(mc3_u32 bit, const char *value, mc3_u32 table, int count, mc3_u32 field,
                  char t0, char t1, char t2, char t3)
{
    const int idx = mc3_rc_lookup(table, count, value);
    if (idx >= 0)
        mc3_rc_store(MC3_RC_CURRENT, field, (mc3_u32)idx);
    rba_state *const s = st();
    if (!(s->reported & bit)) {
        s->reported |= bit;
        tag(t0, t1, t2, t3); hex8((mc3_u32)idx); put(10);
    }
}

extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    const char *const tod = mc3_bootarg(MC3_ID('t', 'i', 'm', 'e'));
    if (tod)
        apply(1u, tod, MC3_RC_T_TOD, MC3_RC_N_TOD, MC3_RC_F_TOD, 'R', 'B', 'T', 'D');

    const char *const weather = mc3_bootarg(MC3_ID('w', 'e', 'a', 't'));
    if (weather)
        apply(2u, weather, MC3_RC_T_WEATHER, MC3_RC_N_WEATHER, MC3_RC_F_WEATHER,
              'R', 'B', 'W', 'E');

    const char *const racetype = mc3_bootarg(MC3_ID('r', 'a', 'c', 'e'));
    if (racetype)
        apply(4u, racetype, MC3_RC_T_RACETYPE, MC3_RC_N_RACETYPE, MC3_RC_F_RACETYPE,
              'R', 'B', 'R', 'T');
}
