// -----------------------------------------------------------------------------
//  native_bootargs - feed the retail game's OWN datArgParser from [boot],
//  instead of "porting" anything from the alpha build.
//
//  THE QUESTION THIS ANSWERS
//
//  The alpha build (Oct/2004) names a fuller datArgParser API - typed Get
//  overloads, GetNum, CMD_OPTION_PREFIX - and its .rodata is full of the
//  flags it drove: "nofe", "car1", "maxopponents", "level", "weather", "tod".
//  Retail SLUS-21355 was checked symbol-by-symbol against it (see FORKS.md):
//  Init/Get/SaveToArchive/RestoreFromArchive/Kill all survive, named, at
//  fixed retail addresses, and Init genuinely runs - called from main() at
//  0x001A0D74, before anything this project had previously hooked.
//
//  What does NOT survive: ResponseFile(int&, char**) (0x001A0CE8), called
//  right before Init, disassembles in retail to exactly `jr ra; nop` - an
//  empty stub. Nothing feeds Init a real argv, and none of the alpha's flag
//  strings exist anywhere in retail's .rodata for anything to Get() even if
//  they did. The parser is intact; its data source and its consumers are
//  both gone.
//
//  So porting is the wrong frame - there is nothing broken to graft back in.
//  This module just gives the still-alive parser a second, real argv: one
//  built from mc3boot.ini's [boot] section by mc3boot.c itself (see
//  reserve_nativeargs/nativearg_add there), in exactly the "-key" /
//  "-key=value" shape Init's own loop already parses. Calling Init again is
//  safe - measured by disassembly, it only walks argv and inserts into its
//  hash table, ordinary heap allocation, nothing tied to a boot-time-only
//  memory state.
//
//      [boot]
//      nofe = 1
//      car1 = vp_eclipse_04
//
//  gives argv = { "-nofe", "-car1=vp_eclipse_04" }, and after this module
//  runs, ANY other module can call datArgParser::Get("nofe") or Get("car1")
//  through the game's own parser - real indexed lookups (Get's retail form
//  is a thin wrapper over the SAME indexed function the alpha's GetNum used,
//  confirmed by disassembly: it forwards to Get(key, 0xFFFF)) rather than
//  this project's own 4-character-folded mc3_bootarg() table.
//
//  This module only revives the parser. It does not reimplement what any
//  flag DOES - the alpha's consumer code for "nofe" (skip the front end) or
//  "car1" (force a car) does not exist in retail and has to be written fresh
//  as ordinary hooks, same as every other mod here. What this proves is that
//  those future hooks can ask the question the alpha's own vocabulary asked,
//  through the game's real datArgParser, fed by [boot] instead of a rebuild.
//
//  THE PROOF, WITHOUT A NEW STRING LITERAL
//
//  Verifying Get() actually works would normally need a key to ask for - but
//  authoring one in this module is exactly the trap docs/WRITING_A_MOD.md
//  warns about (a string literal is its own data base). There is no need:
//  Init's own loop NUL-terminates argv[0] in place at the first '=' it finds
//  (`sb zero, 0(s0)` in its disassembly) or leaves it untouched if there is
//  none, so after calling Init, argv[0]+1 (skip the leading '-') is already
//  exactly the key Get() would want - reusing a pointer that lives in the
//  cave, built by mc3boot.c, never a literal this module wrote itself.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_native_args.h"

enum {
    DATARGPARSER_INIT = 0x00428AC0,   // void Init(int argc, char **argv)
    DATARGPARSER_GET  = 0x004293F0,   // int  Get(const char *key)  - wraps Get(key, 0xFFFF)
};

struct nba_state {
    mc3_u32 done;      // 1 once Init has been called this boot
    mc3_u32 argc;       // what we fed it, for the SIO report
    mc3_u32 get_ok;     // 1 once the self-test Get() call has run
    mc3_u32 get_ret;    // its return value
};
static nba_state g_state;
static __attribute__((noinline)) nba_state *st(void) { return &g_state; }

static void put(char c) { *(volatile unsigned char *)0x1000F180 = (unsigned char)c; }
static void hex8(mc3_u32 v)
{
    for (int i = 28; i >= 0; i -= 4) {
        unsigned d = (v >> i) & 0xF;
        put((char)(d < 10 ? ('0' + d) : ('A' + d - 10)));
    }
}
static void tag(char a, char b, char c, char d) { put(a); put(b); put(c); put(d); put(' '); }

extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    nba_state *const s = st();
    if (s->done)
        return;                          // run-once even if the .ini loads us per-frame
    s->done = 1u;

    mc3_u32 argv_base = 0;
    const mc3_u32 argc = mc3_native_argv(&argv_base);

    tag('N', 'B', 'A', 'R');             // NBAR <argc> - what [boot] gave us before Init
    hex8(argc); put(10);
    s->argc = argc;

    if (!argc)
        return;                          // no [boot] section this boot: nothing to feed

    MC3_CALL2(void, DATARGPARSER_INIT, int, char **)((int)argc, (char **)argv_base);
    tag('N', 'B', 'O', 'K');             // NBOK <argc> - Init ran with this many entries
    hex8(argc); put(10);

    // Self-test: argv[0]+1 is Init's own key text for the first [boot] line,
    // already NUL-terminated at '=' by Init itself if it had a value.
    const mc3_u32 first = *(volatile mc3_u32 *)argv_base;
    const char *const key = (const char *)(first + 1);   // skip the leading '-'
    const int ret = MC3_CALL1(int, DATARGPARSER_GET, const char *)(key);
    s->get_ok = 1u;
    s->get_ret = (mc3_u32)ret;
    tag('N', 'B', 'G', 'T');             // NBGT <Get() return for [boot]'s first key>
    hex8(s->get_ret); put(10);
}
