// -----------------------------------------------------------------------------
//  race_maxopponents - `[boot] maxopponents = <n>`, capping how many of the
//  race's own opponent list actually load.
//
//  WHERE THIS CAME FROM
//
//  The alpha build has PARAM_maxopponents, and disassembling its one real
//  consumer - mcRaceBase::LoadOpponents, the SAME function, same name, in
//  both builds - shows exactly what it does: read the opponent count parsed
//  from the race's own text file into a field on the mcRaceBase instance
//  (offset +20 in the alpha), then clamp that field to
//  min(loaded_count, PARAM_maxopponents) partway through the function.
//
//  Retail keeps the function and the field - just at offset +24 instead of
//  +20 (decompiled: `*(_DWORD *)(a1 + 24) = v6;`, the same count used to size
//  both opponent-array allocations) - and drops the PARAM object and its
//  read. Nothing to revive there either: this mod supplies the missing half
//  from [boot] instead.
//
//  WHY THE CLAMP RUNS *AFTER* THE ORIGINAL RETURNS, NOT INSIDE THE LOOP
//
//  The alpha clamps mid-loop, which also happens to bound the loop's own
//  continuation check - a mid-function patch this project has no easy way to
//  splice into without risking the DisabledAtStart/RemoveAtEnd handling
//  alongside it. Both opponent arrays are already sized for the FULL count
//  read from the file by the time LoadOpponents returns, so lowering the
//  count field afterward is equivalent for anything that reads it next
//  (spawn logic, opponent HUD, CountNumOpponentRacers) - fewer of an
//  already-allocated array get used, never more than were ever loaded, and
//  nothing downstream had a chance to read the old value first since this
//  hook is the very next thing that runs after LoadOpponents returns.
//
//  THE HOOK SITE
//
//  mcRaceBase::LoadOpponents has exactly one caller, mcRaceBase::Load, at
//  0x003D507C. At that instruction $a0 already holds the mcRaceBase `this` -
//  set by the delay slot of the branch just before it - so this hook's own
//  first argument is that same pointer, no different than LoadOpponents'
//  own would have been.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_bootargs.h"

enum {
    SITE           = 0x003D507C,
    LOAD_OPPONENTS = 0x003D6ED8,
    COUNT_OFFSET   = 24,   // *(mcRaceBase* + 24): opponents actually loaded
};

struct rmo_state {
    mc3_u32 applied;   // 1 once the clamp has fired at least once, for the SIO report
};
static rmo_state g_state;
static __attribute__((noinline)) rmo_state *st(void) { return &g_state; }

static void put(char c) { *(volatile unsigned char *)0x1000F180 = (unsigned char)c; }
static void hex8(mc3_u32 v)
{
    for (int i = 28; i >= 0; i -= 4) {
        unsigned d = (v >> i) & 0xF;
        put((char)(d < 10 ? ('0' + d) : ('A' + d - 10)));
    }
}
static void tag(char a, char b, char c, char d) { put(a); put(b); put(c); put(d); put(' '); }

// No libc here (see mc3_mod.h) - [boot] values arrive as plain digit text
// ("8", not 0x8), so this reads them the same way the .ini itself was typed.
static mc3_u32 parse_uint(const char *s)
{
    mc3_u32 v = 0;
    for (; *s >= '0' && *s <= '9'; ++s)
        v = v * 10u + (mc3_u32)(*s - '0');
    return v;
}

extern "C" void race_maxopponents_hook(mc3_u32 race_base, mc3_u32 tokenizer)
{
    MC3_CALL2(void, LOAD_OPPONENTS, mc3_u32, mc3_u32)(race_base, tokenizer);

    const char *const text = mc3_bootarg(MC3_ID('m', 'a', 'x', 'o'));
    if (!text)
        return;                                   // [boot] set nothing: leave the count alone

    const mc3_u32 cap = parse_uint(text);
    volatile mc3_u32 *const count = (volatile mc3_u32 *)(race_base + COUNT_OFFSET);
    if (cap < *count) {
        *count = cap;
        rmo_state *const s = st();
        if (!s->applied) {
            s->applied = 1u;
            tag('R', 'M', 'O', 'K');               // RMOK <count now in the field>
            hex8(*count);
            put(10);
        }
    }
}

MC3_HOOK(SITE, race_maxopponents_hook);
