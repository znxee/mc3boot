// -----------------------------------------------------------------------------
//  core - the module that loads the other modules.
//
//  THE PROBLEM IT SOLVES
//
//  Modules live in the cave: 10 KB of zeroed space inside the game image. That
//  is a hard ceiling on how much mod can exist, and the obvious fixes do not
//  work. Reserving a range before the game boots was tried: raising the heap
//  base so the allocator could not reach it still left the asset loader placing
//  .pck images there, and a module got overwritten by wheel-rim data.
//
//  The reason that failed is that before the game runs, nothing owns the memory.
//  Afterwards its allocator does, and what it hands out it will not hand to
//  anything else. So this asks the game - __builtin_new, the same operator new
//  the executable calls in 1333 places - and loads the deferred modules into
//  what it gets back.
//
//  WHEN IT RUNS, and why there are two answers
//
//  `= defer` loads on the first frame. That is as early as a dispatcher callback
//  can be, and it costs the module every hook that fires before one - which
//  includes Main's init calls, where three mods install themselves.
//
//  `= shim` loads in the EARLY pass instead, hooked at ioFFWheel::InitClass
//  (001A0F28) - still inside Main's init, but ahead of the three sites mods use
//  as an install trigger. init_probe.mod measured that both halves of load_one
//  work there: the allocator returns real blocks and the raw stream API opens a
//  file and reports its exact size.
//
//  The two are kept apart on purpose. Loading every deferred module early would
//  move the ground under mods written against the first-frame contract, since a
//  `defer_once` body runs the moment it is placed. So `shim` opts in.
//
//  This module therefore needs BOTH a hook and a per-frame callback, which is
//  what `core.mod = bootstrap` in the .ini means. Nothing else asks for it.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/mc3_modfmt.h"
#include "../../payload/mc3_sio.h"

// The game's own file API, the same entry points and the same argument the
// in-image loader has been using since the first payload.
enum {
    STREAM_OPEN  = 0x003991F0,   // Stream* Open(const char*, int methods)
    STREAM_READ  = 0x003993A8,   // int Read(Stream*, void*, int)
    STREAM_CLOSE = 0x00399748,   // void Close(Stream*)
    STREAM_SIZE  = 0x003997A8,   // int Size(Stream*)
    // 1 selects the coreRaw method table - raw open/read/close. With 0 the call
    // takes a different structure and returns null.
    STREAM_RAW   = 1,

    GAME_LO = 0x001A0000,
    GAME_HI = 0x00715D3C,
};

// The list mc3boot leaves behind: every module the .ini marked `defer`, as
// NUL-terminated paths. Keep in sync with DEFER in ../../mc3_inject.py.
struct mc3_defer {
    mc3_u32 magic;          // 'MC3D'
    mc3_u32 count;
    mc3_u8  records[1];     // mode byte + NUL-terminated path, packed
};
static const mc3_defer *const g_defer = (const mc3_defer *)0x0061CAE0;
#define DEFER_MAGIC 0x4D433344u

// Results, left where a savestate can be read for them. Same reasoning as the
// loader's boot trace: a module that failed to load is otherwise
// indistinguishable from one that loaded and did nothing.
struct mc3_core_trace {
    mc3_u32 magic;          // 'MC3R'
    mc3_u32 ran;            // times the one-shot body ran (should be 1)
    mc3_u32 wanted;
    mc3_u32 loaded;
    mc3_u32 hooks;
    mc3_u32 last_err;
    mc3_u32 arena;          // what the game gave us
    mc3_u32 arena_used;
};
static mc3_core_trace *const g_trace = (mc3_core_trace *)0x0061CAC0;
#define CORE_MAGIC 0x4D433352u

enum { E_NONE = 0, E_OPEN, E_SIZE, E_ALLOC, E_READ, E_PLACE };

// The mode byte in front of each path in the defer list. Bit 0 is
// once-versus-per-frame; bit 1 says the module came in as `= shim`, and that
// decides WHICH PASS loads it. See the enum in boot/mc3boot.c.
enum {
    DEFER_ONCE = 0, DEFER_FRAME = 1,
    SHIM_ONCE  = 2, SHIM_FRAME  = 3
};
#define MODE_IS_SHIM(m) ((m) & 2u)

// Where the early pass runs, and what the `jal` there used to point at.
// ioFFWheel::InitClass, the last init call in Main before the three that mods
// use as their install trigger:
//
//     001A0F28  ioFFWheel::InitClass   <- here
//     001A0F30  mc::AllocateCarData        menu_row
//     001A0F38  mc::AllocateAudSfx         proper_widescreen_menu
//     001A0F40  mc::AllocateCityData       city_slot6
//
// Measured with init_probe.mod rather than assumed: at this point the game's
// allocator returns real blocks AND the raw stream API opens a file and
// reports its exact size. Both halves of load_one therefore work here, which
// is the entire reason the early pass can exist.
enum {
    EARLY_SITE = 0x001A0F28,
    EARLY_ORIG = 0x0045D618,
};

static int load_one(const char *path, mc3_u8 mode, mc3_u32 *callbacks)
{
    mc3_u32 stream = MC3_CALL2(mc3_u32, STREAM_OPEN, const char *, int)
                        (path, STREAM_RAW);
    if (!stream) { g_trace->last_err = E_OPEN; return 0; }

    const int size = MC3_CALL1(int, STREAM_SIZE, mc3_u32)(stream);
    if (size <= (int)MC3M_HDR) {
        MC3_CALL1(void, STREAM_CLOSE, mc3_u32)(stream);
        g_trace->last_err = E_SIZE;
        return 0;
    }

    // Two blocks: the file as read, and the code placed out of it. The first is
    // handed straight back - it is only needed for the length of this call.
    void *file = mc3_alloc((mc3_u32)size);
    if (!file) {
        MC3_CALL1(void, STREAM_CLOSE, mc3_u32)(stream);
        g_trace->last_err = E_ALLOC;
        return 0;
    }

    const int got = MC3_CALL3(int, STREAM_READ, mc3_u32, void *, int)
                        (stream, file, size);
    MC3_CALL1(void, STREAM_CLOSE, mc3_u32)(stream);
    if (got != size) { mc3_free(file); g_trace->last_err = E_READ; return 0; }

    const mc3_u32 code_size = ((const mc3_u32 *)file)[3];
    void *code = mc3_alloc(code_size);
    if (!code) { mc3_free(file); g_trace->last_err = E_ALLOC; return 0; }

    mc3m_module m;
    const int r = mc3m_place(file, (mc3_u32)size, (mc3_u32)code, &m);
    mc3_free(file);
    if (r != MC3M_OK) {
        mc3_free(code);
        g_trace->last_err = E_PLACE;
        return 0;
    }

    g_trace->hooks += (mc3_u32)mc3m_install_hooks(&m, GAME_LO, GAME_HI);
    if (!m.nhooks) {
        // Bit 0, not an equality test: SHIM_FRAME is 3 and wants a per-frame
        // callback exactly as DEFER_FRAME does. Comparing against DEFER_FRAME
        // would silently demote every hookless `= shim` module to run-once.
        if (mode & 1u) {
            const mc3_u32 n = callbacks[0];
            callbacks[n + 1] = m.entry;
            callbacks[0] = n + 1;
        } else {
            ((void (*)())m.entry)();
        }
    }
    g_trace->arena_used += code_size;
    return 1;
}

// Walks the defer list and loads the entries belonging to one pass. `shim`
// selects which: the early pass takes the modules whose mode has bit 1, the
// first-frame pass takes the rest.
//
// Splitting them is the point. Loading everything early would quietly move the
// ground under every mod written against the first-frame contract - a
// `defer_once` body runs the moment it is placed, and doing that in the middle
// of Main's init is a different thing from doing it on frame one. So `shim`
// opts in and `defer` keeps the behaviour it had.
static void load_pass(int shim, mc3_u32 *callbacks)
{
    const mc3_u8 *p = g_defer->records;
    for (mc3_u32 i = 0; i < g_defer->count; ++i) {
        const mc3_u8 mode = *p++;
        const char *path = (const char *)p;
        if (!MODE_IS_SHIM(mode) == !shim)
            g_trace->loaded += (mc3_u32)load_one(path, mode, callbacks);
        while (*p) ++p;
        ++p;
    }
}

static int g_done;

// Keep the guard address behind a single relocation pair. GCC otherwise reuses
// one HI16 for both the load and store, while the v2 module format records only
// the first LO16 and leaves the store pointing near address zero.
static __attribute__((noinline)) int *done_slot()
{
    return &g_done;
}

// The callback table, allocated by whichever pass runs first. The first word is
// how many follow; the rest are entry addresses. It belongs to the game heap for
// the whole session, and g_trace->arena doubles as the "already allocated" flag
// so the two passes cannot each make one.
static mc3_u32 *ensure_callbacks()
{
    if (g_trace->arena)
        return (mc3_u32 *)g_trace->arena;
    mc3_u32 *cb = (mc3_u32 *)mc3_alloc(
        (g_defer->count + 1u) * (mc3_u32)sizeof(mc3_u32));
    if (!cb) {
        g_trace->last_err = E_ALLOC;
        return 0;
    }
    cb[0] = 0;
    g_trace->arena = (mc3_u32)cb;
    return cb;
}

// Data to this CPU, code to whatever jumps there next. Without it the R5900 can
// run whatever was in its instruction cache - the failure that looks exactly
// like the module never having loaded.
static void flush()
{
    MC3_CALL1(void, 0x00546C20, int)(0);   // FlushCache(0)
    MC3_CALL1(void, 0x00546C20, int)(2);   // FlushCache(2)
}

// THE EARLY PASS. Hooked at ioFFWheel::InitClass, which is the last init call in
// Main before the three that mods use as an install trigger - so a `= shim`
// module's hooks are in place before mc::AllocateCarData, AllocateAudSfx and
// AllocateCityData run.
//
// This is what `= shim` was missing. A shim trampoline claims the site at boot,
// but until the body exists its slot holds the ORIGINAL target, so a one-shot
// init call reaches the original and the module never sees it. Claiming the site
// early was never enough; the BODY has to be there early too. city_slot6,
// menu_row and proper_widescreen_menu all failed for exactly this reason.
extern "C" void core_early()
{
    // The original first, with a0..a3 as the game left them.
    MC3_CALL(void, EARLY_ORIG)();

    g_trace->magic = CORE_MAGIC;
    if (g_defer->magic != DEFER_MAGIC || g_defer->count == 0)
        return;
    g_trace->wanted = g_defer->count;

    mc3_u32 *callbacks = ensure_callbacks();
    if (!callbacks)
        return;
    load_pass(1, callbacks);
    flush();
    mc3_sio_mark(69, 65, 82, 76, g_trace->loaded);   // EARL <loaded so far>
}

// Runs once, on the first frame. Hooked at a per-frame site, so "once" has to be
// enforced by the caller - the same site fires every frame.
static void core_once()
{
    g_trace->magic = CORE_MAGIC;
    g_trace->ran += 1;

    if (g_defer->magic != DEFER_MAGIC || g_defer->count == 0)
        return;

    g_trace->wanted = g_defer->count;

    mc3_u32 *callbacks = ensure_callbacks();
    if (!callbacks)
        return;

    load_pass(0, callbacks);

    flush();

    // What the two passes actually did, out the SIO terminal so the headless
    // boot test can see it. Until this existed the whole heap half of loading
    // was invisible: a module that failed to place and one that placed and did
    // nothing produced identical logs.
    //
    // EARL is printed by the early pass, DEFR here, and SHIM is hooks that
    // landed in a shim slot instead of patching the game word - n > 0 is the
    // proof that boot and a load pass agreed about a site.
    mc3_sio_mark(68, 69, 70, 82, g_trace->loaded);       // DEFR
    mc3_sio_mark(83, 72, 73, 77, MC3M_SHIM_HITS);        // SHIM
}

MC3_HOOK(EARLY_SITE, core_early);

// Called once per frame by the dispatcher, not through MC3_HOOK. Every module
// that wants a frame wants the same four `jal` sites, and two modules cannot
// both own one - the second install would silently replace the first. The
// dispatcher exists precisely so they can share.
extern "C" void mod_main() __attribute__((section(".text.start")));
extern "C" void mod_main()
{
    // The dispatcher still invokes core every frame. Only the installation of
    // the deferred module is one-shot; per-frame modules keep their dispatcher
    // callback semantics.
    int *done = done_slot();
    if (!*done) {
        *done = 1;
        core_once();
    }

    // `defer` means load once into stable heap memory, then call the module's
    // hookless entry every frame. It never means allocating another copy.
    mc3_u32 *callbacks = (mc3_u32 *)g_trace->arena;
    if (callbacks) {
        const mc3_u32 count = callbacks[0];
        for (mc3_u32 i = 0; i < count; ++i)
            ((void (*)())callbacks[i + 1])();
    }
}
