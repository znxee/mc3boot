// -----------------------------------------------------------------------------
//  city_force - override which city the frontend loads in the background,
//  independent of what the memory card save says.
//
//  WHICH CITY, without a rebuild: `[boot] city = <name>` in mc3boot.ini. Named
//  by the same text the .lst/.loc files use ("modcity", "sd", "atlanta", ...);
//  see payload/mc3_bootargs.h for the mechanism and mc3boot's README for the
//  .ini side. No [boot] section, or a name that does not resolve, and this
//  falls back to TARGET_CITY_DEFAULT below.
//
//  WHAT THE HINT LED TO, AND WHERE IT STOPPED
//
//  "The city index comes from the memory card save" pointed at the right
//  field - *(*(0x00619B10)) is exactly the byte city_probe.mod's `SES:` line
//  already reads, and COMO_TESTO.md already established the correlation:
//  SES 00000000 = sd, SES 00000005 = modcity, measured across many boots.
//
//  Finding the EXACT instruction that writes it from the save turned out to be
//  a longer chase than expected, and this module does not finish that chase.
//  Bracketed with three probe hooks along Main_void's init:
//
//      Main+0x1A0F8C  (right after mcConfig::CopyNextToCurrent)   -> 0
//      Main+0x1A12F0  (right after mcGame::InitCareer)            -> 0
//      Main+0x1A12F8  (right after mcMemCard::CreateInstance)     -> 0
//      core.mod's first per-frame dispatch (frame 1)              -> 5
//
//  So the write is not in Main's synchronous init at all - it happens inside
//  mcGame::Execute's own loop, on or before its first iteration, which is
//  outside what a boot-time hook on Main_void can bracket further without
//  hooking into the frontend's own per-frame code. That is real, unfinished
//  work; see city_boot_probe.mod for the measurements above.
//
//  WHY THE CHASE DID NOT NEED FINISHING
//
//  Overriding the field does not require knowing which instruction wrote it -
//  only that the override runs AFTER the game's own write and BEFORE anything
//  renders using the value. The bracket above already proves that: whatever
//  writes it does so before the modloader's first per-frame dispatch (`defer`
//  is hookless and runs every frame starting with that first one), so a
//  hookless module rewriting the field every frame provably wins the race -
//  not by timing luck, but because the alternative was measured impossible.
//
//  Every frame rather than once, on purpose: if career code re-applies the
//  saved value later (menu re-entry, a race ending, anything that runs
//  CopyNextToCurrent again), a one-shot override would be silently reverted.
//  Reasserting it every frame costs one comparison and, ordinarily, nothing
//  else - the write only happens when the value is not already what we want.
//
//  THE GUARD
//
//  Before forcing anything, the target record's name pointer is checked
//  against the table city_slot6.mod already validated. Forcing the frontend
//  into a record nobody registered - the constructor's default
//  "not_initialized" - would be forcing it into exactly the empty slot
//  mc::LoadDefaultCityData already tried and found nothing for, which is a
//  city with no content. Refuse rather than do that silently.
// -----------------------------------------------------------------------------

// The boot-args mechanism this mod is also the working example for: read
// `[boot] city = <name>` from mc3boot.ini and resolve it to a table index at
// run time, instead of only accepting a compiled-in constant. See
// payload/mc3_bootargs.h for what this buys and why the key is four
// characters rather than the string "city".
#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_bootargs.h"

enum {
    SESSAO     = 0x00619B10,   // pointer to the session mcRaceConfig; +0 = city index
    CITY_ARRAY = 0x00619D4C,   // &record[0], the same table city_slot6.mod fills
    CITY_STRIDE = 76,
    CITY_MAX_SCAN = 8,         // stop the name search here even if nothing refuses first

    // The compiled-in fallback, used when [boot] carries no `city` key, or
    // names one that does not resolve. 1 = Atlanta: chosen for the first test
    // because the memory card in use already had 5 (modcity) saved, and
    // forcing a DIFFERENT retail city was the clean, unambiguous proof that
    // this overrides the save rather than coincidentally agreeing with it.
    TARGET_CITY_DEFAULT = 1,

    STRCASECMP = 0x004328A0,
    NOT_INITIALIZED = 0x0066A492,
};

struct force_state {
    mc3_u32 writes;       // how many times the field actually needed correcting
    mc3_u32 refused;      // 1 if the target record looked unregistered
    mc3_u32 from_arg;     // 1 if the active target came from [boot] city, not the default
    mc3_u32 resolved;     // 1 once the target index has been decided for this boot
    mc3_u32 target;       // the index actually in force
};
static force_state g_state;
static __attribute__((noinline)) force_state *st(void) { return &g_state; }

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

// Range only, no alignment - a NAME pointer aims into a packed string table
// ("sd\0atlanta\0detroit\0..."), and a later name in the table lands on
// whatever byte follows the previous name's NUL. "atlanta" measured at
// 0x0066A44D, one past "sd\0": not 4-aligned, and a perfectly good pointer.
// sensato() would have rejected it and ended the whole scan on city index 1,
// which is exactly the bug this project's other mods reading city names
// likely also carry - noted rather than fixed everywhere at once.
static int name_ok(mc3_u32 p) { return p >= 0x00100000u && p < 0x02000000u; }

// Finds a registered record whose name matches, by walking the table directly
// rather than calling the game's own mc::LookupCity - that function's
// not-found path was not one this session pinned down with confidence, and a
// self-contained search means city_force never depends on it. Stops at the
// first NOT_INITIALIZED record, the same boundary city_slot6.mod's guard uses,
// or at CITY_MAX_SCAN if somehow nothing is ever unregistered.
static int find_city_index(mc3_u32 base, const char *name)
{
    for (int i = 0; i < CITY_MAX_SCAN; ++i) {
        const mc3_u32 record = base + (mc3_u32)(i * CITY_STRIDE);
        const mc3_u32 np = *(volatile mc3_u32 *)(record + 0);
        if (np == (mc3_u32)NOT_INITIALIZED)
            break;
        if (!name_ok(np))
            break;
        if (MC3_CALL2(int, STRCASECMP, const char *, const char *)
                ((const char *)np, name) == 0)
            return i;
    }
    return -1;
}

// Decides the target ONCE per boot: [boot] city, resolved by name against the
// live table, or the compiled-in default if the key is absent or does not
// resolve. Deferred to the first call rather than done up front, because the
// city table does not exist yet at module-load time - see city_slot6.mod's
// own comment on why registration rides an init hook instead of running cold.
static mc3_u32 resolve_target(mc3_u32 base)
{
    force_state *const s = st();
    if (s->resolved)
        return s->target;

    const char *const wanted = mc3_bootarg(MC3_ID('c', 'i', 't', 'y'));
    int idx = wanted ? find_city_index(base, wanted) : -1;

    s->from_arg = (idx >= 0) ? 1u : 0u;
    s->target = (idx >= 0) ? (mc3_u32)idx : (mc3_u32)TARGET_CITY_DEFAULT;
    s->resolved = 1u;

    tag('C', 'F', 'A', 'R');                          // CFAR <1=from [boot] city, 0=default>
    hex8(s->from_arg); put(' '); hex8(s->target); put(10);
    return s->target;
}

extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    force_state *const s = st();

    const mc3_u32 ptr = *(volatile mc3_u32 *)SESSAO;
    if (!sensato(ptr))
        return;                                  // not constructed yet

    const mc3_u32 base = *(volatile mc3_u32 *)CITY_ARRAY;
    if (!sensato(base))
        return;

    const mc3_u32 target = resolve_target(base);

    const mc3_u32 record = base + target * CITY_STRIDE;
    const mc3_u32 name = *(volatile mc3_u32 *)(record + 0);
    if (name == (mc3_u32)NOT_INITIALIZED) {
        if (!s->refused) {
            s->refused = 1u;
            tag('C', 'F', 'R', 'F'); hex8(target); put(10);  // CFRF <index refused>
        }
        return;
    }

    volatile mc3_u32 *const idx = (volatile mc3_u32 *)ptr;
    if (*idx != target) {
        *idx = target;
        s->writes += 1u;
        if (s->writes == 1u) {
            tag('C', 'F', 'O', 'K'); hex8(target); put(10);  // CFOK <index now forced>
        }
    }
}
