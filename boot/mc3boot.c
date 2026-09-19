/* ---------------------------------------------------------------------------
 *  mc3boot - a chain loader for Midnight Club 3 (SLUS-21355)
 *
 *  Runs INSTEAD of the game, then hands the machine over to it. The order is
 *  the whole point:
 *
 *      1. read mc3mod.bin            <- a full file system exists here
 *      2. read the game ELF into place
 *      3. write the payload, the in-image loader and the hooks
 *      4. apply the constant patch groups
 *      5. flush the caches, reset the IOP, jump to the game entry
 *
 *  Everything the payload needs is in RAM before the game's first instruction.
 *  That is what neither a pnach nor the boot stub could do: at the end of the
 *  game's crt0 there is no file system yet, so an external payload simply is not
 *  there to be used. Here it is, because we read it while we were still a normal
 *  program.
 *
 *  It also retires the pnach entirely - no PCSX2, no code limits, no CRC, and
 *  the same binary on an emulator and on a real console.
 *
 *  MEMORY. The game is one PT_LOAD covering 0x1A0000..0x715D3C. This loader is
 *  linked at 0x01000000, well above it, and after the jump its memory is free
 *  for the game's heap to reuse - nothing of ours lives there. What has to
 *  survive sits inside the game image itself (the cave at 0x61C858 and the
 *  payload at 0x61D000), which the game never writes.
 * ------------------------------------------------------------------------- */

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <debug.h>

/* POSIX, not fio*. ps2sdk's own headers refuse the fio family outright with
 * "Using fio/fileXio functions directly in the newlib port will lead to
 * problems - use posix function calls instead", and libcglue maps open/read/
 * lseek/close onto the right thing per device. */

#include "addresses.h"
#include "../payload/config.h"
#include "../payload/patches.h"

/* The HostFS bootstrap - the writes that make the game read loose files instead
 * of the disc. It used to be unmovable: as a pnach it had to run before the game
 * opened any file, and the payload IS a file. The chain loader dissolves that,
 * because it reads with its own file system and writes into the image before the
 * game starts.
 *
 * These are ON in the table and gated at RUN TIME instead, on whether the game
 * itself came from host0:. Booting mc3boot.elf changes the ELF CRC, so PCSX2
 * loads no pnach for the game at all - this is now the only thing that applies
 * them, and hard-coding the answer would break whichever setup was not chosen. */
#define MC3BOOT_EN_HOSTFS_ARQUIVOS_SOLTOS_NECESSARIO 1
#define MC3BOOT_EN_HOSTFS_CORRECAO_DE_STREAMS_DAT_MAIOR_QUE_2_GIB 1
#include "bootstrap.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* The loader's screen output scrolls past and needs a person watching it. One
 * byte at a time to 0x1000F180 lands in the PCSX2 log instead, which is what
 * the headless boot test reads - and the arena accounting is the line worth
 * having there, because running out of cave is the failure this loader hits
 * most and the symptom (an infinite load before the frontend) says nothing
 * about the cause. Strings are fine here: mc3boot is an ordinary EE program,
 * not something mc3_mkmod has to pack. */
static void sio_puts(const char *s)
{
    while (*s)
        *(volatile u8 *)0x1000F180 = (u8)*s++;
}

static void sio_hex(u32 v, int digits)
{
    static const char D[] = "0123456789ABCDEF";
    while (digits-- > 0)
        *(volatile u8 *)0x1000F180 = (u8)D[(v >> (digits * 4)) & 0xFu];
}

/* The game boots from a disc image on hardware and through HostFS under PCSX2.
 * Rather than build two loaders, each name is tried in turn and the first that
 * opens wins. host0: comes first, so a loose file on the PC beats the copy baked
 * into the image while you are iterating. */
static const char *GAME_PATHS[] = {
    "host0:slus_213.55.ELF",
    "cdrom0:\\SLUS_213.55;1",
    "mass0:/SLUS_213.55",
    0
};
static const char *MOD_PATHS[] = {
    "host0:mc3mod.bin",
    "cdrom0:\\MC3MOD.BIN;1",
    "mass0:/mc3mod.bin",
    0
};

/* Held out of the way: the game image lands on top of a wide address range, so
 * the payload is read first but written only after the ELF is in place. */
static u8 modbuf[MC3_MAX_SIZE];

/* ---------------------------------------------------------------------------
 *  mc3boot.ini
 *
 *  The point is that turning a mod on or off should not need a toolchain. The
 *  table already carries every group; the .ini only decides which are `enabled`,
 *  so nothing here is a rebuild.
 *
 *      [patches]
 *      Video/Widescreen 16:9 = 1
 *      60 FPS/1 - Destravar para 60 fps = 1
 *
 *      [payload]
 *      dynamic_peds = 1
 *      dynamic_city_rate = 0
 *
 *  Names are exactly the group names the generator emits, which are the pnach
 *  group names with `\` written as `/`. Both separators are accepted, because
 *  typing the pnach name is the obvious thing to do.
 *
 *  Absent file, absent section or absent line all mean "leave as built". So an
 *  .ini that only turns one thing off is a valid .ini.
 * ------------------------------------------------------------------------- */
#define INI_MAX 4096
static char ini_buf[INI_MAX + 1];

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') ++s;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) --e;
    *e = 0;
    return s;
}

/* Group names differ only in the separator, so compare with `\` and `/` treated
 * as the same character. */
static int same_group(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char ca = (*a == '\\') ? '/' : *a;
        char cb = (*b == '\\') ? '/' : *b;
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static int set_group(mc3_group *tab, int count, const char *nome, int val)
{
    int g;
    for (g = 0; g < count; ++g) {
        if (same_group(tab[g].name, nome)) {
            tab[g].enabled = (unsigned char)(val != 0);
            return 1;
        }
    }
    return 0;
}

static int open_first(const char **list, const char **chosen)
{
    int i, fd;
    for (i = 0; list[i]; ++i) {
        fd = open(list[i], O_RDONLY);
        if (fd >= 0) {
            *chosen = list[i];
            return fd;
        }
    }
    return -1;
}

/* Read exactly n bytes, in chunks. One 5 MB fioRead is more than the RPC buffer
 * wants to move at once on some setups, and a short read that goes unnoticed is
 * a game that boots into garbage. */
static int read_all(int fd, void *dst, u32 n)
{
    u8 *p = (u8 *)dst;
    u32 done = 0;
    while (done < n) {
        u32 want = n - done;
        int got;
        if (want > 0x40000u) want = 0x40000u;
        got = read(fd, p + done, (int)want);
        if (got <= 0) break;
        done += (u32)got;
    }
    return (done == n) ? 0 : -1;
}

/* The same table the payload carries, applied here so the values are already in
 * place when the game reads them during its own start-up. Doing it in both
 * places is deliberate and harmless: the payload compares before writing, so
 * from its point of view there is nothing left to do. */
static int apply_table(const mc3_group *tab, int count)
{
    int g, n = 0;
    u16 w;
    for (g = 0; g < count; ++g) {
        if (!tab[g].enabled) continue;
        for (w = 0; w < tab[g].n; ++w) {
            const mc3_write *it = &tab[g].w[w];
            switch (it->size) {
            case 1:  *(volatile u8  *)it->addr = (u8)it->value;  break;
            case 2:  *(volatile u16 *)it->addr = (u16)it->value; break;
            default: *(volatile u32 *)it->addr = it->value;      break;
            }
            ++n;
        }
    }
    return n;
}

/* ---------------------------------------------------------------------------
 *  Modules
 *
 *  A .mod is a relocatable build of a mod: header, code, and a list of the
 *  places whose contents are absolute addresses. Placing one is copy, then walk
 *  the list adding the delta. See ../mc3_mkmod.py for the format and for why the
 *  awkward half (pairing HI16 with LO16) happens at build time instead of here.
 *
 *  This is what lets someone add a mod by dropping a file in and naming it in
 *  the .ini. Without relocation every mod would have to own a fixed address, and
 *  two mods from two authors would collide with no way to tell.
 * ------------------------------------------------------------------------- */
#define MOD_HDR   32
#define T_WORD32  1
#define T_JAL26   2
#define T_HI_LO   3

static u32 mod_next = MC3_MODBASE;      /* bump allocator over the free block */
static u32 mod_tab[32];
static int mod_count;        /* modules wanting the per-frame dispatcher */
static int mod_loaded;       /* modules placed, however they are reached */
static int hook_count;       /* hook sites installed by modules */

/* ---------------------------------------------------------------------------
 *  Hook shims
 *
 *  THE PROBLEM. There are two ways to load a module and each costs what the
 *  other saves. `= 1` puts the whole thing in the cave, so its hooks are live
 *  from boot - and the cave is 10072 bytes for everything. `= defer` puts the
 *  body on the game's heap, where size stops mattering, but the module does not
 *  exist until the first frame and so cannot catch anything before one.
 *
 *  Nothing about a hook needs the body, though. A hook is one word in the game
 *  image, and all it has to do until the body arrives is keep calling whatever
 *  the game called before.
 *
 *  THE SHIM. 24 bytes per hook SITE, and the body defers:
 *
 *      +0   lui  $t9, %hi(slot)
 *      +4   lw   $t9, %lo(slot)($t9)
 *      +8   jr   $t9
 *      +12  nop
 *      +16  slot   <- the original jal target; the body overwrites it
 *      +20  alvo   <- the game word this shim owns, so the body can find it
 *
 *  The slot starts out holding the address the `jal` pointed at, which is what
 *  makes this safe: from boot until the body loads the shim is a two-instruction
 *  detour to the same place, not a hole. When core.mod places the body,
 *  mc3m_install_hooks finds the site in this table and writes the handler into
 *  the slot instead of patching the game word again.
 *
 *  WHAT THE TRAMPOLINE ALONE DOES NOT BUY, learned the hard way. Claiming the
 *  site early is not the same as being able to answer early. Until the body
 *  exists the slot still points at the original, so a site that fires ONCE
 *  before the body loads is missed exactly as it would be with a plain `defer`.
 *  city_slot6, menu_row and proper_widescreen_menu all broke on this: each
 *  hooks one of Main's one-shot init calls at 001A0F30/38/40.
 *
 *  The other half of the fix is therefore in core.cpp - a `= shim` module's
 *  BODY is loaded during Main's init, at 001A0F28, ahead of those three. The
 *  trampoline still earns its place for anything firing before that point, and
 *  for the cave it saves either way.
 *
 *  It costs the module author nothing. The source keeps its MC3_HOOK, the .mod
 *  is the same file - only the .ini line changes, because which of the three
 *  strategies a mod wants is not something the loader can work out.
 *
 *  $t9 is free to clobber: the shim stands where a `jal` stood, and the callee
 *  it replaced was equally free to. Records grow DOWN from the end of the arena
 *  while modules grow up, so the two allocators share the block and meet in the
 *  middle instead of needing a region of their own.
 * ------------------------------------------------------------------------- */
#define SHIM_STRIDE  MC3_SHIM_STRIDE

struct mc3_shim {
    u32 magic;
    u32 count;
    u32 base;        /* lowest record; records run upward from here */
    u32 stride;
    u32 hits;        /* filled in later by the payload, see mc3_modfmt.h */
    u32 orig_base;   /* {addr, original} pairs, see snap_originals */
    u32 orig_n;
    u32 reg_base;    /* {id, fn} pairs, see reserve_registry */
    u32 reg_cap;
    u32 args_base;   /* {id, value[ARGS_VALLEN]} pairs, see the [boot] section */
    u32 args_n;
    /* NOT argv_n too: SHIMHDR..MODTAB is a fixed 48-byte gap (mc3_inject.py's
     * own layout comment), and this struct at 11 u32 fields already used all
     * but the last word of it - a 12th field is the most that fits before
     * spilling into MODTAB's first entry. Measured the hard way: a first
     * attempt at this feature added argv_base AND argv_n here, and the SIO
     * log showed argv_n reading back as whatever MODTAB[0] happened to hold,
     * not what nativearg_add had just written. So argc rides in the array
     * instead - see reserve_nativeargs: a zero argv[] entry ends it, the same
     * "id 0 means free" convention bootarg_slot already uses. */
    u32 argv_base;   /* char* argv[NATIVE_ARGS_CAP], zero-terminated */
};
static struct mc3_shim *const g_shim = (struct mc3_shim *)MC3_SHIMHDR;
static u32 shim_next = MC3_MODEND;      /* bump allocator, downward */

/* Returns the record address, or 0 if the site cannot be shimmed. */
static u32 shim_install(u32 alvo)
{
    volatile u32 *rec;
    u32 slot, w, orig, hi, lo;

    if (alvo < GAME_VA || alvo >= GAME_VA + GAME_MEMSZ || (alvo & 3u))
        return 0;

    /* Only a `jal` can be shimmed, and for the one reason that matters: the
     * fallback has to come from somewhere, and for a jal it is the target the
     * word already encodes. Anything else and there is nothing to fall back to,
     * so refuse rather than guess. */
    w = *(volatile u32 *)alvo;
    if ((w & 0xFC000000u) != 0x0C000000u)
        return 0;
    orig = ((w & 0x03FFFFFFu) << 2) | (alvo & 0xF0000000u);

    if (shim_next - SHIM_STRIDE < mod_next + 16u)
        return 0;
    shim_next -= SHIM_STRIDE;
    rec = (volatile u32 *)shim_next;
    slot = shim_next + 16u;

    /* The low half is sign extended by the `lw`, so the high half carries the
     * borrow - the same correction the HI_LO relocation makes below. */
    lo = slot & 0xFFFFu;
    hi = ((slot >> 16) + ((lo >> 15) & 1u)) & 0xFFFFu;

    rec[0] = 0x3C190000u | hi;          /* lui  $t9, hi      */
    rec[1] = 0x8F390000u | lo;          /* lw   $t9, lo($t9) */
    rec[2] = 0x03200008u;               /* jr   $t9          */
    rec[3] = 0x00000000u;               /* nop               */
    rec[4] = orig;                      /* slot: where the game was going */
    rec[5] = alvo;                      /* and which word sent it there   */

    if (g_shim->magic != MC3_SHIM_MAGIC) {
        g_shim->magic = MC3_SHIM_MAGIC;
        g_shim->count = 0;
        g_shim->stride = SHIM_STRIDE;
        g_shim->hits = 0;
    }
    g_shim->base = shim_next;           /* newest is lowest; count runs up */
    g_shim->count += 1;

    *(volatile u32 *)alvo = 0x0C000000u | ((shim_next >> 2) & 0x03FFFFFFu);
    ++hook_count;
    return shim_next;
}

/* ---------------------------------------------------------------------------
 *  The originals
 *
 *  A patch group is {addr, value, size} and nothing else - it says what to
 *  write, never what was there. That is fine for a table applied once at boot
 *  and fatal for anything that wants to turn a patch back OFF, which is exactly
 *  what an in-game patches menu is.
 *
 *  So every address the table mentions is read HERE, before a single patch is
 *  written, and the word is kept. It has to be all of them and not just the
 *  enabled ones: a group that is off at boot can be switched on in the menu and
 *  then needs its original to switch back off again.
 *
 *  Stored as {addr, original} pairs rather than a bare array in table order, so
 *  a mod looks its address up instead of trusting two walks to agree. 8 bytes
 *  times however many writes the table has - 48 today, 384 bytes.
 * ------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
 *  The export registry
 *
 *  A table of {id, function} so one module can call another's code. Allocated
 *  HERE, at boot, for one reason: it then exists before any module runs, so no
 *  module has to own it and there is no ordering question about the table
 *  itself. Only about who has filled it in yet, which is the caller's problem
 *  and is documented in ../payload/mc3_registry.h.
 *
 *  Zeroed, because an id of 0 is what marks a slot free.
 * ------------------------------------------------------------------------- */
#define REG_CAP 32u

static void reserve_registry(void)
{
    const u32 bytes = REG_CAP * 8u;
    u32 i;

    if (shim_next - bytes < mod_next + 16u)
        return;                 /* no room: modules find no table and cope */
    shim_next -= bytes;

    for (i = 0; i < bytes / 4u; ++i)
        ((volatile u32 *)shim_next)[i] = 0;

    if (g_shim->magic != MC3_SHIM_MAGIC) {
        g_shim->magic = MC3_SHIM_MAGIC;
        g_shim->count = 0;
        g_shim->stride = SHIM_STRIDE;
        g_shim->hits = 0;
        g_shim->base = 0;
    }
    g_shim->reg_base = shim_next;
    g_shim->reg_cap = REG_CAP;
}

/* ---------------------------------------------------------------------------
 *  Boot args: [boot] key = value in the .ini, read back by any module.
 *
 *  WHY THIS EXISTS
 *
 *  Testing a specific scene - "boot straight into modcity", "boot with car X
 *  selected" - used to mean a constant baked into a .cpp, a rebuild, and a
 *  copy to HostFS for every single change. The .ini is already read before any
 *  module runs; this just gives a module a way to ask it a question, the same
 *  way mc3_registry.h lets one module call another's function without either
 *  side owning a fixed address.
 *
 *  THE KEY IS FOUR CHARACTERS, on purpose and for the same reason as the
 *  registry: a string literal in a .mod is a data base, and mc3_mkmod refuses
 *  a build where GCC shares one `lui` across several. So a module never
 *  compares strings - it asks for MC3_ID('c','i','t','y'), the exact macro
 *  payload/mc3_registry.h already defines, reused here rather than duplicated.
 *  An .ini key is folded into the same four bytes by keeping only its first
 *  four characters, so writing `city = modcity` in the .ini and asking for
 *  MC3_ID('c','i','t','y') refer to the same slot without either side needing
 *  to know that about the other beyond the four letters lining up.
 *
 *  THE VALUE is plain text, up to ARGS_VALLEN-1 bytes, because that half of
 *  the trap does not apply here: this file is compiled with a real libc
 *  (mc3boot.elf is the standalone loader, not a freestanding .mod), so
 *  building the table costs nothing unusual. Reading it back from a .mod is
 *  a byte compare against what THIS file already wrote, not a literal the mod
 *  authored itself, so it never becomes a second data base either.
 * ------------------------------------------------------------------------- */
#define ARGS_CAP     16u
#define ARGS_VALLEN  32u
#define ARGS_STRIDE  (4u + ARGS_VALLEN)

static void reserve_bootargs(void)
{
    const u32 bytes = ARGS_CAP * ARGS_STRIDE;
    u32 i;

    if (shim_next - bytes < mod_next + 16u)
        return;
    shim_next -= bytes;

    for (i = 0; i < bytes; ++i)
        ((volatile u8 *)shim_next)[i] = 0;

    if (g_shim->magic != MC3_SHIM_MAGIC) {
        g_shim->magic = MC3_SHIM_MAGIC;
        g_shim->count = 0;
        g_shim->stride = SHIM_STRIDE;
        g_shim->hits = 0;
        g_shim->base = 0;
    }
    g_shim->args_base = shim_next;
    g_shim->args_n = 0;             /* how many are actually SET, not the cap */
}

/* Packs up to the first four characters of an .ini key into the same 32-bit
 * shape MC3_ID(a,b,c,d) builds in payload/mc3_registry.h - see that macro for
 * why it is this order. A key shorter than four characters pads with 0, same
 * as a mod that passes '\0' for characters it does not need. */
static u32 bootarg_id(const char *key)
{
    u32 id = 0;
    int i, ended = 0;
    /* Once the key ends, every remaining slot packs as 0 - the same value a
     * mod gets from passing '\0' to MC3_ID for the characters it does not
     * need, so "car" and MC3_ID('c','a','r','\0') land on the same id.
     * `ended` matters: after the terminator, key[i] is memory this string
     * does not own, so it is never read once seen. */
    for (i = 0; i < 4; ++i) {
        u8 c = 0;
        if (!ended) {
            if (key[i]) c = (u8)key[i];
            else ended = 1;
        }
        id = (id << 8) | (u32)c;
    }
    return id;
}

/* Finds the id's slot, or the first free one when `make` is set. Same shape as
 * mc3_reg_slot in mc3_registry.h: re-setting an existing key replaces it. */
static volatile u8 *bootarg_slot(u32 id, int make)
{
    u32 i;
    volatile u8 *empty = 0;
    if (g_shim->magic != MC3_SHIM_MAGIC || !g_shim->args_base)
        return 0;
    for (i = 0; i < ARGS_CAP; ++i) {
        volatile u8 *rec = (volatile u8 *)(g_shim->args_base + i * ARGS_STRIDE);
        volatile u32 *idp = (volatile u32 *)rec;
        if (*idp == id) return rec;
        if (!*idp && !empty) empty = rec;
    }
    return make ? empty : 0;
}

static void bootarg_set(const char *key, const char *value)
{
    const u32 id = bootarg_id(key);
    volatile u8 *rec = bootarg_slot(id, 1);
    u32 i;
    if (!rec) return;                /* full, or the table did not fit at boot */
    for (i = 0; i < ARGS_VALLEN - 1u && value[i]; ++i)
        rec[4u + i] = (u8)value[i];
    rec[4u + i] = 0;
    /* id LAST: the same reason mc3_export writes it after the pointer - a
     * reader that catches this mid-write should see either nothing or the
     * whole value, never a live id paired with a half-written string. */
    *(volatile u32 *)rec = id;
    g_shim->args_n += 1u;
}

/* ---------------------------------------------------------------------------
 *  Native argv: the same [boot] lines, reshaped into what
 *  datArgParser::Init(int argc, char **argv) actually expects.
 *
 *  bootarg_set above stores each [boot] key as a 4-character id, which is
 *  enough for a .mod to ask "was X set" but throws away everything past the
 *  fourth letter - "maxopponents" and "maxo" fold to the same slot. That is
 *  fine for mc3_bootarg's own lookup (a .mod asks with the same folded id it
 *  wrote), but datArgParser::Get hashes the FULL string the game already
 *  ships with ("maxopponents", not "maxo"), so feeding it a truncated key
 *  would silently never match.
 *
 *  So this keeps the untruncated text instead, formatted the one way
 *  datArgParser::Init actually parses (measured by disassembling it): each
 *  argv[] entry is "-key" or "-key=value", one leading '-', at most one '='.
 *  Init walks argv itself splitting on '=' and inserting into its own
 *  ArgHash - nothing here duplicates that logic, it only builds the array
 *  Init already knows how to read.
 * ------------------------------------------------------------------------- */
#define NATIVE_ARGS_CAP    16u
#define NATIVE_ARG_LEN     48u   /* "-maxopponents=whatever", NUL included */

/* How many argv[] slots are filled so far. Kept here rather than as another
 * shim-header field - see the field's own comment on why only one more word
 * fit. Nothing outside this file needs the count during boot: a .mod finds
 * it by scanning argv[] for the zero entry, same contract as real argv/argc
 * conventions where a NULL terminator makes the array self-describing. */
static u32 nativeargs_n = 0;

static void reserve_nativeargs(void)
{
    const u32 pool_bytes = NATIVE_ARGS_CAP * NATIVE_ARG_LEN;
    const u32 argv_bytes = (NATIVE_ARGS_CAP + 1u) * 4u;  /* +1: the NULL terminator slot */
    u32 i;

    if (shim_next - (pool_bytes + argv_bytes) < mod_next + 16u)
        return;
    shim_next -= argv_bytes;
    for (i = 0; i < argv_bytes / 4u; ++i) ((volatile u32 *)shim_next)[i] = 0;
    g_shim->argv_base = shim_next;

    shim_next -= pool_bytes;
    for (i = 0; i < pool_bytes; ++i) ((volatile u8 *)shim_next)[i] = 0;
    /* g_native_pool: not kept as its own variable - argv_base - pool_bytes
     * is always where it lands, and nativearg_add below recomputes it the
     * same way rather than adding a field nothing else needs. */

    if (g_shim->magic != MC3_SHIM_MAGIC) {
        g_shim->magic = MC3_SHIM_MAGIC;
        g_shim->count = 0;
        g_shim->stride = SHIM_STRIDE;
        g_shim->hits = 0;
        g_shim->base = 0;
    }
    nativeargs_n = 0;
}

static void nativearg_add(const char *key, const char *value)
{
    u32 pool, slot, n, i, j;

    if (!g_shim->argv_base || nativeargs_n >= NATIVE_ARGS_CAP)
        return;                          /* table did not fit, or is full */

    n = nativeargs_n;
    pool = g_shim->argv_base - NATIVE_ARGS_CAP * NATIVE_ARG_LEN;
    slot = pool + n * NATIVE_ARG_LEN;

    j = 0;
    ((volatile char *)slot)[j++] = '-';
    for (i = 0; key[i] && j < NATIVE_ARG_LEN - 2u; ++i)
        ((volatile char *)slot)[j++] = key[i];
    /* An empty or "1" value means a bare flag ("-nofe"), same convention the
     * alpha's own vocabulary used for on/off switches - anything else is
     * carried as "-key=value" for datArgParser::Get's indexed/typed forms. */
    if (value[0] && !(value[0] == '1' && value[1] == 0)) {
        ((volatile char *)slot)[j++] = '=';
        for (i = 0; value[i] && j < NATIVE_ARG_LEN - 1u; ++i)
            ((volatile char *)slot)[j++] = value[i];
    }
    ((volatile char *)slot)[j] = 0;

    ((volatile u32 *)g_shim->argv_base)[n] = slot;
    /* [n + 1] stays 0 - reserve_nativeargs zeroed the whole array and this
     * only ever fills it front to back, so the slot right after n is always
     * either untouched (still the NULL terminator) or the next entry a later
     * call will overwrite in turn. */
    nativeargs_n = n + 1u;
}

static u32 snap_originals(const mc3_group *tab, int count)
{
    int g;
    u16 w;
    u32 n = 0, need = 0;
    volatile u32 *out;

    for (g = 0; g < count; ++g)
        need += tab[g].n;
    if (!need)
        return 0;

    /* Out of the same block the shims come from, and by the same rule: if it
     * does not fit, nothing is captured and the menu simply finds no table. */
    if (shim_next - need * 8u < mod_next + 16u)
        return 0;
    shim_next -= need * 8u;
    out = (volatile u32 *)shim_next;

    for (g = 0; g < count; ++g) {
        for (w = 0; w < tab[g].n; ++w) {
            const mc3_write *it = &tab[g].w[w];
            u32 v;
            switch (it->size) {
            case 1:  v = *(volatile u8  *)it->addr; break;
            case 2:  v = *(volatile u16 *)it->addr; break;
            default: v = *(volatile u32 *)it->addr; break;
            }
            out[n * 2] = it->addr;
            out[n * 2 + 1] = v;
            ++n;
        }
    }

    if (g_shim->magic != MC3_SHIM_MAGIC) {
        g_shim->magic = MC3_SHIM_MAGIC;
        g_shim->count = 0;
        g_shim->stride = SHIM_STRIDE;
        g_shim->hits = 0;
        g_shim->base = 0;
    }
    g_shim->orig_base = shim_next;
    g_shim->orig_n = n;
    return n;
}

/* ---------------------------------------------------------------------------
 *  Boot trace
 *
 *  The loader prints why it refused a module, but the screen scrolls past and a
 *  savestate is what actually gets kept. Everything it prints about modules is
 *  recorded here as well, at a fixed address inside the game image, so the
 *  reason survives into the savestate and can be read back with
 *  `mc3_inject.py state`.
 *
 *  This exists because a module failing to load is otherwise indistinguishable
 *  from a module that loaded and did nothing.
 * ------------------------------------------------------------------------- */
#define TRACE_ADDR   0x0061CA60u
#define TRACE_MAGIC  0x4D433354u        /* 'MC3T' */

enum {
    ERR_NONE = 0, ERR_OPEN, ERR_SHORT, ERR_MAGIC, ERR_VERSION,
    ERR_TRUNC, ERR_FULL, ERR_SPACE, ERR_RELOC
};

struct mc3_trace {
    u32 magic;
    u32 ini_index;      /* which INI_PATHS entry opened, or ~0 */
    u32 tried;          /* module lines acted on */
    u32 loaded;         /* modules actually placed */
    u32 last_err;       /* one of the ERR_ codes */
    u32 last_read;      /* bytes the failing read returned */
    char last_path[72]; /* the path that failed, as the loader built it */
};
static struct mc3_trace *const g_trace = (struct mc3_trace *)TRACE_ADDR;

static void trace_fail(const char *path, u32 err, u32 got)
{
    u32 i;
    g_trace->last_err = err;
    g_trace->last_read = got;
    for (i = 0; i < sizeof(g_trace->last_path) - 1 && path[i]; ++i)
        g_trace->last_path[i] = path[i];
    g_trace->last_path[i] = 0;
}

/* One module at a time, so this only has to hold the largest single .mod, not
 * all of them. 64 KB against a 256 KB region: a module bigger than this is
 * almost certainly a mistake, and it is refused by name either way. */
static u8 mod_buf[65536];

/* Returns 0 on success. Everything that can go wrong is reported by name: a mod
 * that silently does not load is indistinguishable from a mod that does nothing,
 * and the person hitting it may not have a compiler, let alone a debugger. */
static int load_module(const char *path, int want_frame)
{
    u32 magic, ver, entry, tam, nrel, base, delta, dest, g_off, g_n;
    u32 i;
    int fd, n = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        trace_fail(path, ERR_OPEN, (u32)n);
        scr_printf("  mod      %s: not found\n", path);
        return 1;
    }
    n = (int)read(fd, mod_buf, sizeof(mod_buf));
    close(fd);
    if (n < MOD_HDR) {
        trace_fail(path, ERR_SHORT, (u32)n);
        scr_printf("  mod      %s: too short (%d bytes)\n", path, n);
        return 1;
    }
    magic = *(u32 *)(mod_buf + 0);
    ver   = *(u32 *)(mod_buf + 4);
    entry = *(u32 *)(mod_buf + 8);
    tam   = *(u32 *)(mod_buf + 12);
    nrel  = *(u32 *)(mod_buf + 16);
    base  = *(u32 *)(mod_buf + 20);
    g_off = *(u32 *)(mod_buf + 24);
    g_n   = *(u32 *)(mod_buf + 28);
    if (magic != MC3_MOD_MAGIC) {
        trace_fail(path, ERR_MAGIC, (u32)n);
        scr_printf("  mod      %s: not a .mod (magic %08X)\n", path, magic);
        return 1;
    }
    if (ver != 2) {
        trace_fail(path, ERR_VERSION, (u32)n);
        scr_printf("  mod      %s: version %u, expected 2\n", path, ver);
        return 1;
    }
    if ((u32)n < MOD_HDR + tam + nrel * 12u) {
        trace_fail(path, ERR_TRUNC, (u32)n);
        scr_printf("  mod      %s: truncated\n", path);
        return 1;
    }
    if (mod_count >= 31) {
        trace_fail(path, ERR_FULL, (u32)n);
        scr_printf("  mod      %s: table full\n", path);
        return 1;
    }
    /* Against shim_next, not MC3_MODEND: the shims are bump-allocated downward
     * out of this same block, so the ceiling moves as they are installed. */
    dest = (mod_next + 15u) & ~15u;
    if (dest + tam > shim_next) {
        trace_fail(path, ERR_SPACE, (u32)n);
        scr_printf("  mod      %s: does not fit (%u bytes, %u left)\n",
                   path, tam, shim_next > dest ? shim_next - dest : 0u);
        return 1;
    }

    memcpy((void *)dest, mod_buf + MOD_HDR, tam);
    delta = dest - base;

    for (i = 0; i < nrel; ++i) {
        const u32 *r = (const u32 *)(mod_buf + MOD_HDR + tam + i * 12u);
        volatile u32 *p = (volatile u32 *)(dest + r[1]);
        switch (r[0]) {
        case T_WORD32:
            *p += delta;
            break;
        case T_JAL26: {
            u32 w = *p;
            u32 target = ((w & 0x3FFFFFFu) << 2) | (dest & 0xF0000000u);
            target += delta;
            *p = (w & 0xFC000000u) | ((target >> 2) & 0x3FFFFFFu);
            break;
        }
        case T_HI_LO: {
            volatile u32 *q = (volatile u32 *)(dest + r[2]);
            int low = (int)(*q & 0xFFFFu);
            u32 value, nb, na;
            if (low & 0x8000) low -= 0x10000;
            value = (((*p & 0xFFFFu) << 16) + (u32)low) + delta;
            nb = value & 0xFFFFu;
            /* The low half is sign extended when the pair is added back
             * together, so the high half has to carry the borrow. */
            na = ((value - (u32)(int)(short)nb) >> 16) & 0xFFFFu;
            *p = (*p & 0xFFFF0000u) | na;
            *q = (*q & 0xFFFF0000u) | nb;
            break;
        }
        default:
            scr_printf("  mod      %s: unknown relocation type %u\n", path, r[0]);
            return 1;
        }
    }

    /* The hook table, read AFTER relocation: the handler pointers in it are
     * ordinary absolute addresses and were fixed up by the pass above, so by now
     * they point at the real code.
     *
     * A module that declares hooks is reached through them and does not want the
     * per-frame callback as well; one that declares none is a plain per-frame
     * module and goes in the dispatcher table, exactly as before. */
    if (g_n) {
        const u32 *tab = (const u32 *)(dest + g_off);
        for (i = 0; i < g_n; ++i) {
            u32 alvo = tab[i * 2];
            u32 fn   = tab[i * 2 + 1];
            if (alvo < GAME_VA || alvo >= GAME_VA + GAME_MEMSZ || (alvo & 3u)) {
                scr_printf("  mod      %s: hook target %08X out of the image\n",
                           path, alvo);
                continue;
            }
            *(volatile u32 *)alvo = 0x0C000000u | ((fn >> 2) & 0x3FFFFFFu);
            ++hook_count;
        }
    }
    /* A module with hooks does not want the per-frame callback as well, and a
     * module with none is a plain per-frame module - that is the rule and it
     * has not changed. `want_frame` is the one exception, asked for by name in
     * the .ini with `= bootstrap`: core needs BOTH, because it takes an early
     * hook to load the shimmed modules during Main's init and still has to run
     * the deferred modules' per-frame callbacks afterwards. Nothing else uses
     * it, and nothing else should. */
    if (!g_n || want_frame)
        mod_tab[mod_count++] = dest + entry;
    ++mod_loaded;
    mod_next = dest + tam;
    scr_printf("  mod      %-22s %08X +%u bytes, %u relocs, %u hooks\n",
               path, dest, tam, nrel, g_n);
    return 0;
}

/* ---------------------------------------------------------------------------
 *  Deferred modules
 *
 *  A list of paths left in the image for core.mod to pick up on the first frame.
 *  It is only a list - nothing is read here, because the point of deferring is
 *  that the memory does not exist yet.
 * ------------------------------------------------------------------------- */
/* From addresses.h, which gera_addresses.py writes out of mc3_inject.py. These
 * used to be three literals here, and the end of the list was one of them - so
 * when the report window was carved out of the tail of this region, nothing in
 * this file would have noticed. */
#define DEFER_ADDR   MC3_DEFER
#define DEFER_MAGIC  MC3_DEFER_MAGIC
#define DEFER_END    MC3_DEFER_END

struct mc3_defer {
    u32 magic;
    u32 count;
    u8  records[1];       /* mode byte, then a NUL-terminated path */
};
static struct mc3_defer *const g_defer = (struct mc3_defer *)DEFER_ADDR;
static u8 *defer_next;

/* The mode byte in front of each path. The low bit is once-versus-per-frame,
 * bit 1 says the module asked for a shim - and that second bit is what decides
 * WHEN the body is loaded, not just how its hooks were taken:
 *
 *   defer  -> core loads it on the first frame, as it always has
 *   shim   -> core loads it from inside Main's init, at MC3_EARLY_SITE
 *
 * Keeping them apart matters. Loading every deferred module early would change
 * the behaviour of mods that were written against the first-frame contract -
 * a `defer_once` entry runs its body immediately on load, and running one in
 * the middle of Main's init is not the same thing at all. So `shim` opts in and
 * `defer` is left exactly as it was. */
enum {
    DEFER_ONCE = 0, DEFER_FRAME = 1,
    SHIM_ONCE  = 2, SHIM_FRAME  = 3
};
#define MODE_IS_SHIM(m)  ((m) & 2u)

static int defer_add(const char *path, u8 mode)
{
    u32 path_len = (u32)strlen(path);
    u32 stored_len = path_len;
    u32 len;

    /* The game's coreRaw disc backend canonicalizes cdrom0: names itself and
     * ALWAYS appends ";1" before issuing the open. mc3boot/newlib needs the
     * ISO-9660 version suffix while it is still the active program, but handing
     * that same spelling to core.mod makes the game ask for "...;1;1". Keep
     * the boot-time path intact and strip only the queued copy. */
    if (path_len >= 9u &&
        !strncmp(path, "cdrom", 5) && path[6] == ':' &&
        path[path_len - 2u] == ';' && path[path_len - 1u] == '1')
        stored_len -= 2u;

    len = stored_len + 2u; /* mode + path terminator */
    if (!defer_next) {
        g_defer->magic = DEFER_MAGIC;
        g_defer->count = 0;
        defer_next = g_defer->records;
    }
    if ((u32)(defer_next + len) > DEFER_END) {
        scr_printf("  mod      %s: deferred list full\n", path);
        return 0;
    }
    *defer_next++ = mode;
    memcpy(defer_next, path, stored_len);
    defer_next[stored_len] = 0;
    defer_next += stored_len + 1u;
    g_defer->count += 1;
    scr_printf("  mod      %-22s %s (%s)\n", path,
               MODE_IS_SHIM(mode) ? "early" : "deferred",
               (mode & 1u) ? "per-frame" : "once");
    return 1;
}

/* `= shim`: take the hook sites now, load the body later.
 *
 * Only the HOOK TABLE is read here, and only its first word per entry. That
 * word is the game address to patch, a plain constant the build tool writes
 * straight through - the handler beside it is the one that carries a
 * relocation, and nothing here needs it. So the file is read but nothing is
 * placed, and the cave pays 24 bytes a hook instead of the whole module.
 *
 * A module with no hooks gets deferred plainly. There is nothing to shim, and
 * failing the line over it would be a worse answer than doing the useful half.
 */
static int shim_add(const char *path, u8 mode)
{
    u32 magic, ver, tam, nrel, g_off, g_n, i, done = 0;
    int fd, n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        trace_fail(path, ERR_OPEN, 0);
        scr_printf("  mod      %s: not found\n", path);
        return 0;
    }
    n = (int)read(fd, mod_buf, sizeof(mod_buf));
    close(fd);
    if (n < MOD_HDR) {
        trace_fail(path, ERR_SHORT, (u32)n);
        scr_printf("  mod      %s: too short (%d bytes)\n", path, n);
        return 0;
    }
    magic = *(u32 *)(mod_buf + 0);
    ver   = *(u32 *)(mod_buf + 4);
    tam   = *(u32 *)(mod_buf + 12);
    nrel  = *(u32 *)(mod_buf + 16);
    g_off = *(u32 *)(mod_buf + 24);
    g_n   = *(u32 *)(mod_buf + 28);
    if (magic != MC3_MOD_MAGIC) {
        trace_fail(path, ERR_MAGIC, (u32)n);
        scr_printf("  mod      %s: not a .mod (magic %08X)\n", path, magic);
        return 0;
    }
    if (ver != 2) {
        trace_fail(path, ERR_VERSION, (u32)n);
        scr_printf("  mod      %s: version %u, expected 2\n", path, ver);
        return 0;
    }
    if ((u32)n < MOD_HDR + tam + nrel * 12u) {
        trace_fail(path, ERR_TRUNC, (u32)n);
        scr_printf("  mod      %s: truncated\n", path);
        return 0;
    }

    if (g_n && g_off + g_n * 8u <= tam) {
        const u32 *tab = (const u32 *)(mod_buf + MOD_HDR + g_off);
        for (i = 0; i < g_n; ++i)
            if (shim_install(tab[i * 2]))
                ++done;
    }

    if (!defer_add(path, mode))
        return 0;
    if (g_n)
        scr_printf("  mod      %-22s %u/%u hook sites shimmed, %u bytes\n",
                   path, done, g_n, done * SHIM_STRIDE);
    return 1;
}

/* Returns the payload feature flags, defaulting to what the payload was built
 * with so that no .ini and the old behaviour are the same thing. */
static u32 read_ini(u32 flags)
{
    static const char *INI_PATHS[] = {
        "host0:mc3boot.ini", "cdrom0:\\MC3BOOT.INI;1", "mass0:/mc3boot.ini", 0
    };
    const char *name;
    char *p, *fim, *sec = "";
    int fd, n, applied = 0, ignored = 0;

    for (n = 0; n < (int)sizeof(*g_trace); ++n)
        ((volatile u8 *)g_trace)[n] = 0;
    g_trace->magic = TRACE_MAGIC;
    g_trace->ini_index = ~0u;

    fd = open_first(INI_PATHS, &name);
    if (fd < 0) {
        scr_printf("  ini      none, using the compiled-in settings\n");
        return flags;
    }
    { int k; for (k = 0; INI_PATHS[k]; ++k) if (INI_PATHS[k] == name) g_trace->ini_index = (u32)k; }
    n = (int)read(fd, ini_buf, INI_MAX);
    close(fd);
    if (n <= 0) return flags;
    ini_buf[n] = 0;

    for (p = ini_buf; *p; p = fim) {
        char *line, *eq, *key, *value;
        for (fim = p; *fim && *fim != '\n'; ++fim) { }
        if (*fim) *fim++ = 0;

        line = trim(p);
        if (!*line || *line == ';' || *line == '#') continue;
        if (*line == '[') {
            char *e = strchr(line, ']');
            if (e) { *e = 0; sec = trim(line + 1); }
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        key = trim(line);
        value = trim(eq + 1);

        if (!strcmp(sec, "mods")) {
            /* `something.mod = 1`          into the cave, alive from boot
             * `something.mod = defer`      loaded on the first frame; modules
             *                               without hooks get a frame callback
             * `something.mod = defer_once` loaded once; a hookless entry runs
             *                               once immediately after loading
             * `something.mod = shim`       hook sites taken at boot for 24
             *                               bytes each, body deferred
             * `something.mod = shim_once`  the same, with the once behaviour
             *
             * The cave is 10072 bytes and that is the whole reason the other
             * two exist. `defer` gives the body unlimited room but costs
             * boot-time hooks: it does not exist until the first frame, so it
             * cannot intercept anything before one. `shim` buys those hooks
             * back for 24 bytes a site - see shim_install. Which of the three a
             * mod wants is not something the loader can guess, so it is written
             * down. */
            if (*value == 'd' || *value == 'D' ||
                *value == 's' || *value == 'S') {
                char full[128];
                int pre = 0;
                const int is_shim = (*value == 's' || *value == 'S');
                const int once = (!strcmp(value, "defer_once") ||
                                  !strcmp(value, "defer-once") ||
                                  !strcmp(value, "shim_once") ||
                                  !strcmp(value, "shim-once"));
                u8 mode = (u8)((is_shim ? 2u : 0u) | (once ? 0u : 1u));
                const char *colon = strchr(key, ':') ? 0 : strchr(name, ':');
                if (colon) {
                    pre = (int)(colon - name) + 1;
                    memcpy(full, name, pre);
                }
                strncpy(full + pre, key, sizeof(full) - 1 - pre);
                full[sizeof(full) - 1] = 0;
                if (is_shim ? shim_add(full, mode) : defer_add(full, mode))
                    ++applied;
                else
                    ++ignored;
            } else if (*value != '0') {
                char target[128];
                /* `= bootstrap` is `= 1` plus the per-frame callback a hooked
                 * module normally gives up. Only core asks for it. */
                const int want_frame = (*value == 'b' || *value == 'B');
                if (strchr(key, ':'))
                    strncpy(target, key, sizeof(target) - 1);
                else {
                    /* Bare name: same device the .ini came from. */
                    const char *colon = strchr(name, ':');
                    int pre = colon ? (int)(colon - name) + 1 : 0;
                    memcpy(target, name, pre);
                    strncpy(target + pre, key, sizeof(target) - 1 - pre);
                }
                target[sizeof(target) - 1] = 0;
                g_trace->tried += 1;
                if (load_module(target, want_frame) == 0) {
                    g_trace->loaded += 1;
                    ++applied;
                } else ++ignored;
            }
        } else if (!strcmp(sec, "payload")) {
            u32 bit = 0;
            if (!strcmp(key, "dynamic_peds"))      bit = MC3_F_DYNAMIC_PEDS;
            else if (!strcmp(key, "dynamic_city_rate")) bit = MC3_F_DYNAMIC_CITY_RATE;
            if (bit) {
                if (*value == '0') flags &= ~bit; else flags |= bit;
                ++applied;
            } else ++ignored;
        } else if (!strcmp(sec, "patches")) {
            int v = (*value != '0');
            /* Deliberately only the payload table. The HostFS bootstrap is not
             * a choice - without it the game reads the disc - and letting an
             * .ini turn it off would break booting in a way that looks like the
             * loader failing. The loader decides that one from the device. */
            if (set_group(mc3_groups, MC3_NGROUPS, key, v))
                ++applied;
            else {
                /* Naming a group that does not exist is almost always a typo,
                 * and a silently ignored line is how you spend an evening
                 * wondering why nothing changed. */
                scr_printf("  ini      unknown group: %s\n", key);
                ++ignored;
            }
        } else if (!strcmp(sec, "boot")) {
            /* Free-form: any key, any value. Which keys mean anything is up to
             * whichever module reads them - this section does not know and
             * does not validate, the same way [mods] does not know what a
             * module's own hooks do. */
            bootarg_set(key, value);
            nativearg_add(key, value);
            ++applied;
        }
    }
    scr_printf("  ini      %s  %d lines applied, %d ignored\n",
               name, applied, ignored);

    /* Groups that write the same address are alternatives, and the generated
     * header turns two of them on into a #error. An .ini goes around that check
     * entirely, so it has to be redone here - otherwise the last group in the
     * table silently wins and the setting you can see is not the one in effect. */
    {
        int a, b;
        u16 i, j;
        for (a = 0; a < MC3_NGROUPS; ++a) {
            if (!mc3_groups[a].enabled) continue;
            for (b = a + 1; b < MC3_NGROUPS; ++b) {
                if (!mc3_groups[b].enabled) continue;
                for (i = 0; i < mc3_groups[a].n; ++i)
                    for (j = 0; j < mc3_groups[b].n; ++j)
                        if (mc3_groups[a].w[i].addr == mc3_groups[b].w[j].addr) {
                            scr_printf("  ini      CONFLICT at %08X:\n           %s\n           %s\n",
                                       mc3_groups[a].w[i].addr,
                                       mc3_groups[a].name, mc3_groups[b].name);
                            scr_printf("           enable only one; the second one wins\n");
                            i = mc3_groups[a].n;
                            break;
                        }
            }
        }
    }
    return flags;
}

int main(int argc, char *argv[])
{
    const char *name;
    int fd, i, writes, do_host;
    u32 modsz = 0, flags;

    SifInitRpc(0);
    /* Reset the IOP up front so our own modules load into a known state, the
     * way a normal boot would leave it. */
    while (!SifIopReset("", 0)) { }
    while (!SifIopSync()) { }
    SifInitRpc(0);
    SifLoadFileInit();

    init_scr();
    scr_printf("\n  mc3boot\n\n");

    /* ---- 1. the payload, while a file system still exists ---------------- */
    fd = open_first(MOD_PATHS, &name);
    if (fd < 0) {
        scr_printf("  payload  mc3mod.bin not found, continuing without it\n");
    } else {
        int fim = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        modsz = (fim > 0) ? (u32)fim : 0;
        if (modsz == 0 || modsz > MC3_MAX_SIZE) {
            scr_printf("  payload  mc3mod.bin is %u bytes, the cave holds %u\n",
                       modsz, (u32)MC3_MAX_SIZE);
            modsz = 0;
        } else if (read_all(fd, modbuf, modsz) < 0) {
            scr_printf("  payload  short read, continuing without it\n");
            modsz = 0;
        } else {
            scr_printf("  payload  %-26s %u bytes\n", name, modsz);
        }
        close(fd);
    }

    /* ---- 2. the game ----------------------------------------------------- */
    fd = open_first(GAME_PATHS, &name);
    if (fd < 0) {
        scr_printf("\n  game ELF not found. Tried:\n");
        for (i = 0; GAME_PATHS[i]; ++i) scr_printf("    %s\n", GAME_PATHS[i]);
        SleepThread();
    }
    /* Which device the GAME came from decides whether the HostFS bootstrap is
     * wanted. Reading it off the path that actually opened means one binary
     * serves both setups and neither needs a rebuild. */
    do_host = (strncmp(name, "host0:", 6) == 0);
    scr_printf("  game     %s\n", name);

    lseek(fd, (int)GAME_FILE_OFF, SEEK_SET);
    if (read_all(fd, (void *)GAME_VA, GAME_FILESZ) < 0) {
        scr_printf("\n  short read on the game image - stopping\n");
        close(fd);
        SleepThread();
    }
    close(fd);
    /* .bss: counted in memsz, absent from the file. The game assumes zeros -
     * and our own cave lives in that range, so this has to happen before the
     * injection below, not after. */
    memset((void *)(GAME_VA + GAME_FILESZ), 0, GAME_MEMSZ - GAME_FILESZ);
    scr_printf("  image    %08X + %08X, bss to %08X\n",
               (u32)GAME_VA, (u32)GAME_FILESZ, (u32)(GAME_VA + GAME_MEMSZ));

    /* ---- 3. the .ini ------------------------------------------------------ */
    /* Read AFTER the image is in place: the .ini decides what `enabled` means
     * for the tables below, and everything it writes - the flag block, the
     * modules - lives inside the game image, which the memset above would wipe.
     * It also has to come BEFORE the injection, because whether any module got
     * loaded is what decides the shape of the injection. */
    flags = 0;
#if MC3_DYNAMIC_PEDS
    flags |= MC3_F_DYNAMIC_PEDS;
#endif
#if MC3_DYNAMIC_CITY_RATE
    flags |= MC3_F_DYNAMIC_CITY_RATE;
#endif
    /* Modules go in the cave, inside the game image. An earlier version carved
     * 256 KB off the front of the heap by raising the SetupHeap base, on the
     * reasoning that the allocator could then never reach it. That reasoning was
     * incomplete: the asset loader puts .pck images from the end of the image
     * onwards no matter where the heap starts, and it wrote straight over a
     * module - the address a hook jumped into held "Rims" and .pck padding, and
     * the game hung before its first frame. 10 KB that nothing touches beats
     * 256 KB that the game reuses. */
    /* Before read_ini, because read_ini is what loads the modules and a `= 1`
     * module's hooks are live from boot - so the export table has to exist
     * before the first one of them can run. */
    reserve_registry();
    reserve_bootargs();
    reserve_nativeargs();

    flags = read_ini(flags);
    *(volatile u32 *)MC3_CFG       = MC3_CFG_MAGIC;
    *(volatile u32 *)(MC3_CFG + 4) = flags;

    /* ---- 4. the injection ------------------------------------------------- */
    /* The cave loader always jumps to PAYLOAD+4. What sits there depends on what
     * was found:
     *
     *   modules listed in the .ini  -> the dispatcher, calling each in turn
     *   nothing but mc3mod.bin      -> that payload, exactly as before
     *
     * The second case exists so a setup that works does not stop working when a
     * new mechanism arrives. The two cannot coexist - both want PAYLOAD+4 - and
     * modules win, because that is the one the .ini asked for explicitly. */
    if (hook_count)
        scr_printf("  hooks    %d sites installed by modules\n", hook_count);

    /* The arena report used to be printed here, and was wrong for it: the patch
     * originals are captured in step 5, below, so a line printed in step 4 said
     * the block was emptier than it is. It now goes out after that step. */

    if (mod_count) {
        *(volatile u32 *)MC3_PAYLOAD = MC3_MAGIC;
        for (i = 0; i < MC3_DISPATCH_WORDS; ++i)
            *(volatile u32 *)(MC3_DISPATCH + 4u * i) = mc3_dispatch_code[i];
        for (i = 0; i < mod_count; ++i)
            *(volatile u32 *)(MC3_MODTAB + 4u * i) = mod_tab[i];
        *(volatile u32 *)(MC3_MODTAB + 4u * mod_count) = 0;   /* terminator */
        if (modsz)
            scr_printf("  note     mc3mod.bin ignored: the .ini lists modules\n");
        scr_printf("  inject   dispatcher %08X, %d modules, %u bytes free\n",
                   (u32)MC3_DISPATCH, mod_count, MC3_MODEND - mod_next);
    } else if (mod_loaded) {
        /* Every module reached itself through its own hooks, so the cave, the
         * in-image loader and the fixed hook list are all unnecessary. Nothing
         * is written into the cave at all - the thing that used to be the whole
         * mechanism is now just a fallback for modules that do not say where
         * they want to run. */
        scr_printf("  inject   %d modules, all self-hooked, %u bytes free\n",
                   mod_loaded, MC3_MODEND - mod_next);
    } else if (modsz) {
        memcpy((void *)MC3_PAYLOAD, modbuf, modsz);
        scr_printf("  inject   payload %08X (legacy mode, no modules)\n",
                   (u32)MC3_PAYLOAD);
    }

    /* The cave loader and the fixed hook list belong to the dispatcher path and
     * to the legacy payload. A module that carries its own hooks needs neither,
     * and writing them anyway would put a second `jal` on the same four sites. */
    if (mod_count || (modsz && !mod_loaded)) {
        for (i = 0; i < MC3_CAVE_WORDS; ++i)
            *(volatile u32 *)(MC3_CAVE + 4u * i) = mc3_cave_code[i];
        *(volatile u32 *)MC3_STATE = 2;        /* 2 = loaded; nothing to open */
        for (i = 0; i < MC3_HOOK_WORDS; ++i)
            *(volatile u32 *)mc3_hooks[i] = MC3_HOOK_JAL;
        scr_printf("  cave     %08X, %d hooks\n", (u32)MC3_CAVE, MC3_HOOK_WORDS);
    }

    /* ---- 5. the constant groups ------------------------------------------- */
    /* Before the first patch is written, not after: this is the only moment the
     * original words still exist. Without it patches_menu.mod can turn a patch
     * on and never off again. */
    snap_originals(mc3_groups, MC3_NGROUPS);
    writes = apply_table(mc3_groups, MC3_NGROUPS);
    scr_printf("  patches  %d writes\n", writes);

    /* The one line the headless test greps for, printed LAST so it accounts for
     * every allocation - modules, shims and the patch originals. Modules grow up
     * from base and shims grow down from end, so `free` is what is genuinely
     * left between them, not a figure derived from a fixed ceiling. */
    sio_puts("MC3ARENA base=");   sio_hex(MC3_MODBASE, 8);
    sio_puts(" end=");            sio_hex(MC3_MODEND, 8);
    sio_puts(" mods_top=");       sio_hex(mod_next, 8);
    sio_puts(" shims_bot=");      sio_hex(shim_next, 8);
    sio_puts(" used=");           sio_hex((mod_next - MC3_MODBASE) +
                                          (MC3_MODEND - shim_next), 5);
    sio_puts(" free=");           sio_hex(shim_next > mod_next
                                          ? shim_next - mod_next : 0u, 5);
    sio_puts(" shims=");          sio_hex(g_shim->magic == MC3_SHIM_MAGIC
                                          ? g_shim->count : 0u, 3);
    sio_puts(" orig=");           sio_hex(g_shim->magic == MC3_SHIM_MAGIC
                                          ? g_shim->orig_n : 0u, 3);
    sio_puts(" reg=");            sio_hex(g_shim->magic == MC3_SHIM_MAGIC
                                          ? g_shim->reg_cap : 0u, 3);
    sio_puts(" args=");           sio_hex(g_shim->magic == MC3_SHIM_MAGIC
                                          ? g_shim->args_n : 0u, 3);
    sio_puts(" argv=");           sio_hex(nativeargs_n, 3);
    sio_puts(" hooks=");          sio_hex((u32)hook_count, 3);
    sio_puts(" mods=");           sio_hex((u32)mod_loaded, 3);
    sio_puts("\n");

    /* The HostFS bootstrap, and only when the game itself came from host0:.
     * Applying it against a disc would point the game at files that are not
     * there; skipping it under HostFS is what made the first attempt boot only
     * with the full ISO. */
    if (do_host) {
        writes = apply_table(mc3boot_groups, MC3BOOT_NGROUPS);
        scr_printf("  hostfs   %d writes\n", writes);
    } else {
        scr_printf("  hostfs   skipped, the game did not come from host0:\n");
    }

    /* ---- 6. hand over ---------------------------------------------------- */
    /* Everything written above is data as far as this CPU is concerned, and the
     * game is about to execute most of it. Without the flush below the R5900 can
     * run stale words out of its instruction cache - the one failure mode that
     * looks exactly like the patch never having been applied. */
    /* Hand-over follows ps2sdk's own elf-loader (ee/elf-loader/src/elf.c): drop
     * RPC, flush, ExecPS2. No second IOP reset - the reference does not do one,
     * and the game reboots the IOP itself with its IOPRP280.IMG anyway. Adding
     * one here was a guess, and a guess at this point costs a black screen.
     *
     * The game's crt0 re-runs SetupThread and SetupHeap, so it inherits nothing
     * from us that matters. ExecPS2 starts a program already in memory; it does
     * not reload or clear it, which is the whole reason it is the right call. */
    scr_printf("  entering %08X\n", (u32)GAME_ENTRY);
    SifExitRpc();
    FlushCache(0);
    FlushCache(2);

    ExecPS2((void *)GAME_ENTRY, 0, argc, argv);
    SleepThread();
    return 0;
}
