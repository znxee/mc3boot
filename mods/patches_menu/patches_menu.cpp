// -----------------------------------------------------------------------------
//  patches_menu - the [patches] section of mc3boot.ini, as a screen in the game
//
//  THE SCREEN IS ALREADY BUILT AND NOBODY USES IT
//
//  mcMenuBrain::CreateWidgets constructs twenty-two menu screens at boot, and
//  one of them - "MenuVideo" - is never navigated to. It is a complete
//  mcMenuVideo object living at brain+0x34, with widgets, a list and a
//  PopulateMenu, and nothing reaches it.
//
//  So this does not build a screen. It takes that one:
//
//    * mcMenuBrain::RequestState(brain, 10) opens it. The 10 is measured, not
//      guessed - RequestState's jumptable at 00339E78 has `case 10` executing
//      `lw $s0, 0x34($s0)`, and 0x34 is the slot CreateWidgets stored MenuVideo
//      in at 003430BC. (An earlier reading took the constructor's `li $a2, 0xA`
//      for the id; that argument is 0xA for all twenty-two screens and means
//      something else entirely.)
//
//    * Exactly ONE word in the image holds mcMenuVideo::PopulateMenu, its vtable
//      slot at 0062AD6C. Pointing that at our own builder replaces the screen's
//      contents wholesale, which is safe precisely because nothing else goes
//      there. The original is never called - but SetHeading is, because the
//      original was the only thing calling it and without that the screen keeps
//      whatever title the previous one left up.
//
//  WHERE THE ENTRY ROW GOES, after two wrong answers
//
//  On the TOP-LEVEL menu, right below Aspect Ratio. Not the Options list: that
//  screen is "MenuSimple", built by the generic mcScreenBase constructor from a
//  .ui asset, so it has no PopulateMenu to hook at all - it is absent from all
//  twenty of mcMenuShell::SetHeading's call sites for that reason. Camera
//  Options (mcMenuGeneral) and Cheat Codes were both tried and both are a level
//  too deep to find.
//
//  The top-level menu is mcMenuModeSelect, and proper_widescreen_menu.mod
//  already redirects its vtable slot to add Aspect Ratio. Chaining is how they
//  coexist: each module saves whatever the slot held, installs itself, and calls
//  the saved one first. Installing LATER therefore means appearing LOWER, and
//  this module installs at 001A0F5C against that module's 001A0F38 - which is
//  what puts Patches directly under Aspect Ratio rather than above it.
//
//  ALTERNATIVES CYCLE, THEY ARE NOT SEPARATE SWITCHES
//
//  Five of the twenty-two groups are "Cidade/Orcamento para N fps" and exactly
//  one may be on. As on/off rows that is five rows that silently fight; as one
//  row it is "Orcamento para: 60 fps" and you press it to reach 90, 120, 240, 1
//  and off, the way Aspect Ratio rotates.
//
//  Which groups are alternatives is NOT hardcoded. patches.h expresses it as
//  compile-time #error blocks, and the rule behind those is simply that the
//  groups write the same address - so that rule is applied here at run time
//  instead, over the transitive closure of shared addresses. The row's label is
//  the members' common name prefix and each option is what is left over, so
//  both come out of the table too.
//
//  TURNING A PATCH OFF NEEDS SOMETHING THE TABLE DOES NOT HAVE
//
//  A group is {addr, value, size} - what to write, never what was there. Fine
//  for a table applied once at boot, useless for a toggle. mc3boot reads every
//  address the table mentions BEFORE writing any of them and leaves the words as
//  {addr, original} pairs; see snap_originals in boot/mc3boot.c. This module
//  finds that table through the header at 0061D050.
//
//  WHETHER A GROUP IS CURRENTLY ON is not tracked anywhere, deliberately: the
//  .ini decides it at boot and the menu changes it afterwards, so a third copy
//  of the truth would be one too many. It is read off the machine - a group is
//  on when the word at its first address already equals the value it writes.
//
//  WHAT TAKES EFFECT WHEN. The write always happens immediately; whether the
//  GAME notices depends on when it next reads that code, and for the Video and
//  Graphical families that is scene or video-mode setup rather than every frame.
//  Those rows end in "*" rather than being hidden, because a row that silently
//  does nothing is worse than one that says when it will.
// -----------------------------------------------------------------------------

#include "../../payload/mc3_mod.h"
#include "../../payload/patches.h"

enum {
    // The patches screen: mcMenuVideo, taken over whole.
    VIDEO_VTABLE_SLOT = 0x0062AD6C,
    VIDEO_POPULATE    = 0x00364358,
    VIDEO_STATE       = 10,

    // The top-level menu: mcMenuModeSelect, shared with proper_widescreen_menu.
    MODE_VTABLE_SLOT  = 0x00629C6C,
    MODE_POPULATE     = 0x00354010,

    // Where the vtable redirects are installed. mcControlConfigMgr::Load, the
    // call after the three init slots the other modules already own - and late
    // enough that proper_widescreen_menu (001A0F38) has installed first, which
    // is what orders the two rows.
    INIT_SITE         = 0x001A0F5C,
    INIT_ORIG         = 0x004BAC28,

    SET_HEADING       = 0x00339070,   // mcMenuShell::SetHeading(const char*)
    GOTO_SCREEN       = 0x00346AE8,   // (brain, state) -> RequestState

    SCREEN_ROOT       = 0x00617ADC,   // dword_617ADC; +12 is the shell
    SCREEN_OWNER      = 12,
    SCREEN_LIST       = 0xF0,         // shell+240 is the mcUiListMenu

    ADD_ITEM_SIMPLE   = 0x005881A8,   // returns the row, which SetTextElement wants
    SET_TEXT_ELEMENT  = 0x00588540,
    DAT_CALLBACK_MAKE = 0x0042A630,
    PMF_TOKYO         = 0x00655C44,   // a retail pmf, for its adjustment word
    ITEM_KIND         = 2,
    FLUSH_CACHE       = 0x00546C20,

    // mc3boot's runtime table. orig_base/orig_n are the {addr, original} pairs.
    SHIM_HDR          = 0x0061D050,
    SHIM_MAGIC        = 0x4D433353u,

    MAX_GROUPS        = 32,
    MAX_ROWS          = 24,
    LABEL_CHARS       = 40,
    // The names are written for a config file, not a menu: at full length the
    // list drew them off the right of the screen.
    NAME_CHARS        = 26,
};

struct pm_state {
    mc3_u32 prev_mode_populate; /* what the top-level slot held before us */
    mc3_u32 taken_video;
    mc3_u32 builds;
    mc3_u32 presses;

    /* Families: fam[i] is the representative group of group i. */
    mc3_u32 fam[MAX_GROUPS];
    mc3_u8  dup[MAX_GROUPS];        /* a byte-for-byte repeat of an earlier one */
    int     nfam;
    int     fam_head[MAX_ROWS];     /* family -> its first group index */

    int     row_fam[MAX_ROWS];      /* row -> family */
    mc3_u32 row_item[MAX_ROWS];     /* row -> what AddItemSimple returned */
    mc3_u32 row_cell[MAX_ROWS];     /* row -> its own index, bound to the cb */
    mc3_u32 list;
    int     nrows;

    char    heading[16];
    mc3_u16 label[MAX_ROWS][LABEL_CHARS];
    mc3_u16 entry_label[16];
};

static pm_state g_state;

// One data base behind one noinline accessor. Loose globals and string literals
// each become another, and mc3_mkmod refuses the build with "LO16 sem HI16"
// when GCC shares a single `lui` across two of them.
static __attribute__((noinline)) pm_state *st(void) { return &g_state; }

// The patch table is a second base, so it gets the same treatment.
static __attribute__((noinline)) mc3_group *groups(void) { return mc3_groups; }

static int valid_pointer(mc3_u32 p)
{
    return p >= 0x00100000u && p < 0x02000000u;
}

// ---------------------------------------------------------------------------
//  Reading and writing a patch
// ---------------------------------------------------------------------------

static mc3_u32 read_sized(mc3_u32 addr, unsigned char size)
{
    if (size == 1) return *(volatile mc3_u8 *)addr;
    if (size == 2) return *(volatile mc3_u16 *)addr;
    return *(volatile mc3_u32 *)addr;
}

static void write_sized(mc3_u32 addr, mc3_u32 v, unsigned char size)
{
    if (size == 1)      *(volatile mc3_u8 *)addr = (mc3_u8)v;
    else if (size == 2) *(volatile mc3_u16 *)addr = (mc3_u16)v;
    else                *(volatile mc3_u32 *)addr = v;
}

// The original word for an address, from mc3boot's snapshot. Reports failure
// through `ok` rather than guessing: writing a wrong "original" over live code
// is worse than refusing to turn the patch off.
static mc3_u32 original_of(mc3_u32 addr, int *ok)
{
    const volatile mc3_u32 *h = (const volatile mc3_u32 *)SHIM_HDR;
    *ok = 0;
    if (h[0] != SHIM_MAGIC)
        return 0;
    const mc3_u32 base = h[5], n = h[6];
    if (!base || !n)
        return 0;
    for (mc3_u32 i = 0; i < n; ++i) {
        if (*(const volatile mc3_u32 *)(base + i * 8u) == addr) {
            *ok = 1;
            return *(const volatile mc3_u32 *)(base + i * 8u + 4u);
        }
    }
    return 0;
}

static int group_is_on(const mc3_group *g)
{
    if (!g->n)
        return 0;
    return read_sized(g->w[0].addr, g->w[0].size) == g->w[0].value;
}

static int groups_overlap(const mc3_group *a, const mc3_group *b)
{
    for (unsigned short i = 0; i < a->n; ++i)
        for (unsigned short j = 0; j < b->n; ++j)
            if (a->w[i].addr == b->w[j].addr)
                return 1;
    return 0;
}

static void group_off(const mc3_group *g)
{
    for (unsigned short i = 0; i < g->n; ++i) {
        int ok;
        const mc3_u32 orig = original_of(g->w[i].addr, &ok);
        if (ok)
            write_sized(g->w[i].addr, orig, g->w[i].size);
    }
}

static void group_on(const mc3_group *g)
{
    for (unsigned short i = 0; i < g->n; ++i)
        write_sized(g->w[i].addr, g->w[i].value, g->w[i].size);
}

// ---------------------------------------------------------------------------
//  Families
//
//  Two groups belong together when they write the same address - which is the
//  same rule patches.h turns into its "ligue so um" #error blocks. Taking the
//  transitive closure means a chain like 640x480-progressivo, which overlaps
//  both 640x448 and the 640-wide render targets, pulls all three into one row.
//  That is stricter than the pairwise rules and deliberately so: it can only
//  ever refuse a combination, never allow one the table forbids.
// ---------------------------------------------------------------------------

// Everything after the LAST '/': the section prefix is the same for every row in
// a run and so carries no information once they are listed together.
static const char *after_last_slash(const char *name)
{
    const char *p = name, *last = name;
    for (; *p; ++p)
        if (*p == '/')
            last = p + 1;
    return last;
}

// Drops a leading "1 - ", "2a - " numbering. The numbering exists to order the
// groups in a config file and is noise in a menu - EXCEPT where it is the only
// thing separating two names, and that case is handled by making those two a
// family whose options are what differs further along.
static const char *strip_numbering(const char *s)
{
    const char *p = s;
    while (*p >= '0' && *p <= '9')
        ++p;
    if (p == s)
        return s;
    if (*p >= 'a' && *p <= 'z')
        ++p;
    if (p[0] == ' ' && p[1] == '-' && p[2] == ' ')
        return p + 3;
    return s;
}

// The name a row is built from: no section, no numbering.
static const char *plain_name(const mc3_group *g)
{
    return strip_numbering(after_last_slash(g->name));
}

static int fam_find(int i)
{
    pm_state *const s = st();
    while ((int)s->fam[i] != i)
        i = (int)s->fam[i];
    return i;
}

static void build_families(void)
{
    pm_state *const s = st();
    mc3_group *const tab = groups();
    int n = MC3_NGROUPS;
    if (n > MAX_GROUPS)
        n = MAX_GROUPS;

    for (int i = 0; i < n; ++i) {
        s->fam[i] = (mc3_u32)i;
        s->dup[i] = 0;
    }

    // "60 FPS" and "60 FPS/1 - Destravar para 60 fps" are not alternatives -
    // they are the same single write listed twice, and presenting them as a
    // two-option family produces a row that cycles between two spellings of one
    // thing. A group whose writes match an earlier group's exactly is dropped.
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i && !s->dup[i]; ++j) {
            if (s->dup[j] || tab[i].n != tab[j].n)
                continue;
            int same = 1;
            for (unsigned short w = 0; w < tab[i].n && same; ++w)
                if (tab[i].w[w].addr != tab[j].w[w].addr ||
                    tab[i].w[w].value != tab[j].w[w].value)
                    same = 0;
            if (same)
                s->dup[i] = 1;
        }

    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (!s->dup[i] && !s->dup[j] && groups_overlap(&tab[i], &tab[j])) {
                const int a = fam_find(i), b = fam_find(j);
                if (a != b)
                    s->fam[a < b ? b : a] = (mc3_u32)(a < b ? a : b);
            }

    // One row per family, in the order the table lists their first member, so
    // the screen reads in the same order the .ini does.
    s->nfam = 0;
    for (int i = 0; i < n && s->nfam < MAX_ROWS; ++i)
        if (!s->dup[i] && fam_find(i) == i)
            s->fam_head[s->nfam++] = i;
}

static int fam_member(int famidx, int k, int *out)
{
    pm_state *const s = st();
    const int head = s->fam_head[famidx];
    int n = MC3_NGROUPS, seen = 0;
    if (n > MAX_GROUPS)
        n = MAX_GROUPS;
    for (int i = 0; i < n; ++i)
        if (!s->dup[i] && fam_find(i) == head) {
            if (seen == k) { *out = i; return 1; }
            ++seen;
        }
    return 0;
}

static int fam_size(int famidx)
{
    pm_state *const s = st();
    const int head = s->fam_head[famidx];
    int n = MC3_NGROUPS, count = 0;
    if (n > MAX_GROUPS)
        n = MAX_GROUPS;
    for (int i = 0; i < n; ++i)
        if (!s->dup[i] && fam_find(i) == head)
            ++count;
    return count;
}

// Which member is on, or -1 for none.
static int fam_current(int famidx)
{
    mc3_group *const tab = groups();
    const int count = fam_size(famidx);
    for (int k = 0; k < count; ++k) {
        int g;
        if (fam_member(famidx, k, &g) && group_is_on(&tab[g]))
            return k;
    }
    return -1;
}

// Exactly one member on, or none. The others are reverted first, which is what
// keeps two alternatives from half-overwriting each other.
static void fam_select(int famidx, int which)
{
    mc3_group *const tab = groups();
    const int count = fam_size(famidx);
    for (int k = 0; k < count; ++k) {
        int g;
        if (fam_member(famidx, k, &g) && k != which && group_is_on(&tab[g]))
            group_off(&tab[g]);
    }
    if (which >= 0) {
        int g;
        if (fam_member(famidx, which, &g))
            group_on(&tab[g]);
    }
    // These are instructions as often as constants, and the R5900 will happily
    // keep running the old ones out of its instruction cache otherwise.
    MC3_CALL1(void, FLUSH_CACHE, int)(0);
    MC3_CALL1(void, FLUSH_CACHE, int)(2);
}

// ---------------------------------------------------------------------------
//  Row text
// ---------------------------------------------------------------------------

// How many leading characters every member of a family shares, measured over
// the PLAIN names - section and numbering already gone - and trimmed back to a
// word boundary so an option starts mid-nothing.
//
// Measuring it after the numbering is stripped is what makes the flash-loading
// pair work: "2a - Flash da tela de loading @ 60 fps" and "2b - ... @ 120 fps"
// share nothing at all from character zero, but as plain names they share
// "Flash da tela de loading @ " and the options come out "60 fps" and "120 fps".
static int fam_prefix_len(int famidx)
{
    mc3_group *const tab = groups();
    const int count = fam_size(famidx);
    int g0;
    if (count < 2 || !fam_member(famidx, 0, &g0))
        return 0;

    const char *const a = plain_name(&tab[g0]);
    int len = 0;
    for (;; ++len) {
        const char c = a[len];
        if (!c)
            break;
        int same = 1;
        for (int k = 1; k < count; ++k) {
            int g;
            if (!fam_member(famidx, k, &g) || plain_name(&tab[g])[len] != c) {
                same = 0;
                break;
            }
        }
        if (!same)
            break;
    }
    while (len > 0 && a[len - 1] != ' ')
        --len;
    return len;
}

// The section: what comes BEFORE the last '/'. Used as the row label when the
// members share no usable prefix - "Video/Modo 640x448" against
// "Video/Render targets em 640 de largura" have nothing in common past the
// slash, and "Video" is a better name for that row than either member.
static int copy_section(const char *name, mc3_u16 *out, int limit)
{
    int last = -1;
    for (int i = 0; name[i]; ++i)
        if (name[i] == '/')
            last = i;
    if (last <= 0)
        return 0;
    int n = 0;
    for (int i = 0; i < last && n < limit; ++i)
        out[n++] = (mc3_u16)(unsigned char)name[i];
    return n;
}

// "Video/..." and "Graphical/..." are read when a scene or a video mode is set
// up, not per frame, so they take hold on the next load rather than at once.
static int needs_reload(const char *name)
{
    return (name[0] == 'V' && name[1] == 'i' && name[2] == 'd')
        || (name[0] == 'G' && name[1] == 'r' && name[2] == 'a');
}

// The game's font is sixteen bits per character - an 8-bit literal draws as
// garbage - so ASCII is widened as it is copied.
static void build_label(int row, int famidx)
{
    pm_state *const s = st();
    mc3_group *const tab = groups();
    mc3_u16 *const out = s->label[row];
    int n = 0;
    int g0;

    if (!fam_member(famidx, 0, &g0))
        return;

    const int count = fam_size(famidx);
    const int cur = fam_current(famidx);
    int plen = fam_prefix_len(famidx);

    if (count > 1) {
        const char *const a = plain_name(&tab[g0]);
        for (int i = 0; i < plen && n < NAME_CHARS; ++i)
            out[n++] = (mc3_u16)(unsigned char)a[i];
        /* a label should not end on the separator it was cut at */
        while (n > 0 && (out[n - 1] == 32 || out[n - 1] == 45 ||
                         out[n - 1] == 64 || out[n - 1] == 58))
            --n;
        if (n < 3) {                       /* nothing shared worth showing */
            n = copy_section(tab[g0].name, out, NAME_CHARS);
            plen = 0;
        }
    }
    if (n == 0) {
        for (const char *p = plain_name(&tab[g0]); *p && n < NAME_CHARS; ++p)
            out[n++] = (mc3_u16)(unsigned char)*p;
        plen = 0;
    }

    out[n++] = 58;  out[n++] = 32;                       /* ": " */

    if (cur < 0) {
        out[n++] = 68; out[n++] = 69; out[n++] = 83; out[n++] = 76;  /* DESL */
    } else if (count == 1) {
        out[n++] = 76; out[n++] = 73; out[n++] = 71;                 /* LIG  */
    } else {
        int g;
        if (fam_member(famidx, cur, &g)) {
            const char *opt = plain_name(&tab[g]) + plen;
            for (; *opt && n < LABEL_CHARS - 4; ++opt)
                out[n++] = (mc3_u16)(unsigned char)*opt;
        }
    }

    if (needs_reload(tab[g0].name) && n < LABEL_CHARS - 3) {
        out[n++] = 32;  out[n++] = 42;                   /* " *" */
    }
    out[n] = 0;
}

// ---------------------------------------------------------------------------
//  The screen
// ---------------------------------------------------------------------------

extern "C" void patch_row_pressed(mc3_u32 bound);
extern "C" void open_patches(mc3_u32 bound);

static __attribute__((noinline)) mc3_u32 patch_row_fn(void)
{
    return (mc3_u32)(void *)&patch_row_pressed;
}
static __attribute__((noinline)) mc3_u32 open_patches_fn(void)
{
    return (mc3_u32)(void *)&open_patches;
}

// A row that runs code of ours rather than one that merely navigates. The
// eight-byte pointer-to-member goes to datCallback BY VALUE in a1, and its low
// word is an adjustment taken from a retail pmf rather than invented.
//
// `bound` becomes the callback's `this`, and because the adjustment says the
// pointer-to-member is not virtual, nothing dereferences it looking for a
// vtable - so any valid address rides through to the handler untouched. That is
// what lets each row carry its own index without this module having to know
// where mcUiListMenu keeps its cursor.
static mc3_u32 add_row(mc3_u32 list, mc3_u32 bound, mc3_u32 fn,
                       const mc3_u16 *text)
{
    const mc3_u32 adjustment = *(const volatile mc3_u32 *)(PMF_TOKYO + 0);
    const unsigned long long pmf =
        ((unsigned long long)fn << 32) | (unsigned long long)adjustment;

    mc3_u32 callback[8];
    MC3_CALL3(void, DAT_CALLBACK_MAKE, void *, unsigned long long, mc3_u32)
        (callback, pmf, bound);

    return MC3_CALL6(mc3_u32, ADD_ITEM_SIMPLE, mc3_u32, int, int, void *, int,
                     const mc3_u16 *)(list, 0, 0, callback, ITEM_KIND, text);
}

// Replaces mcMenuVideo::PopulateMenu through the vtable. `this` is the screen.
extern "C" void patches_populate(mc3_u32 self)
{
    pm_state *const s = st();
    ++s->builds;

    const mc3_u32 root = *(volatile mc3_u32 *)SCREEN_ROOT;
    if (!valid_pointer(root))
        return;
    const mc3_u32 shell = *(volatile mc3_u32 *)(root + SCREEN_OWNER);
    if (!valid_pointer(shell))
        return;
    const mc3_u32 list = *(volatile mc3_u32 *)(shell + SCREEN_LIST);
    if (!valid_pointer(list))
        return;

    // The heading. Replacing the whole PopulateMenu dropped the only SetHeading
    // call this screen had, so it kept the title the previous screen left up and
    // read "CAMERA". The argument is a string-table KEY, and a key with no entry
    // is drawn as its own literal text - "PATCHES" has no entry, which is
    // exactly why it works. Built character by character because a literal would
    // be a second data base.
    char *const h = s->heading;
    int hn = 0;
    h[hn++] = 80; h[hn++] = 65; h[hn++] = 84; h[hn++] = 67;   /* PATC */
    h[hn++] = 72; h[hn++] = 69; h[hn++] = 83;                 /* HES  */
    h[hn] = 0;
    typedef void (*head_fn)(mc3_u32, const char *);
    ((head_fn)SET_HEADING)(shell, h);

    build_families();

    s->list = list;
    s->nrows = 0;
    for (int f = 0; f < s->nfam && s->nrows < MAX_ROWS; ++f) {
        const int row = s->nrows;
        s->row_fam[row] = f;
        s->row_cell[row] = (mc3_u32)row;
        build_label(row, f);
        s->row_item[row] = add_row(list, (mc3_u32)&s->row_cell[row],
                                   patch_row_fn(), s->label[row]);
        ++s->nrows;
    }
    (void)self;
}

// Pressing a row advances its family one step: member 0, 1, ... then off, then
// round again. A lone group has one member, so it is just on and off.
extern "C" void patch_row_pressed(mc3_u32 bound)
{
    pm_state *const s = st();
    ++s->presses;

    if (!bound)
        return;
    const int row = (int)*(volatile mc3_u32 *)bound;
    if (row < 0 || row >= s->nrows)
        return;

    const int f = s->row_fam[row];
    const int count = fam_size(f);
    int next = fam_current(f) + 1;
    if (next >= count)
        next = -1;
    fam_select(f, next);

    // AddItemSimple copied the text into the row, so changing our buffer alone
    // changes nothing on screen. Rewriting the row is what makes the label move
    // with the press instead of only on the next visit.
    build_label(row, f);
    if (valid_pointer(s->list) && valid_pointer(s->row_item[row]))
        MC3_CALL4(void, SET_TEXT_ELEMENT, mc3_u32, mc3_u32, int,
                  const mc3_u16 *)(s->list, s->row_item[row], ITEM_KIND,
                                   s->label[row]);
}

// The top-level "Patches" row: ask the brain for the state that lands on
// mcMenuVideo.
extern "C" void open_patches(mc3_u32 /*bound*/)
{
    const mc3_u32 root = *(volatile mc3_u32 *)SCREEN_ROOT;
    if (!valid_pointer(root))
        return;
    MC3_CALL2(void, GOTO_SCREEN, mc3_u32, int)(root, VIDEO_STATE);
}

static __attribute__((noinline)) mc3_u32 mode_populate_fn(void);

// Replaces mcMenuModeSelect::PopulateMenu, chaining whatever held the slot -
// which is proper_widescreen_menu's handler, so Aspect Ratio is added first and
// Patches lands directly beneath it.
extern "C" void mode_populate(mc3_u32 self)
{
    pm_state *const s = st();
    mc3_u32 previous = s->prev_mode_populate;
    if (!valid_pointer(previous) || previous == mode_populate_fn())
        previous = (mc3_u32)MODE_POPULATE;
    MC3_CALL1(void, previous, mc3_u32)(self);

    const mc3_u32 root = *(volatile mc3_u32 *)SCREEN_ROOT;
    if (!valid_pointer(root))
        return;
    const mc3_u32 screen = *(volatile mc3_u32 *)(root + SCREEN_OWNER);
    if (!valid_pointer(screen))
        return;
    const mc3_u32 list = *(volatile mc3_u32 *)(screen + SCREEN_LIST);
    if (!valid_pointer(list))
        return;

    mc3_u16 *const t = s->entry_label;
    int n = 0;
    t[n++] = 80; t[n++] = 97; t[n++] = 116; t[n++] = 99;
    t[n++] = 104; t[n++] = 101; t[n++] = 115;            /* Patches */
    t[n] = 0;
    add_row(list, self, open_patches_fn(), t);
}

static __attribute__((noinline)) mc3_u32 mode_populate_fn(void)
{
    return (mc3_u32)(void *)&mode_populate;
}
static __attribute__((noinline)) mc3_u32 patches_populate_fn(void)
{
    return (mc3_u32)(void *)&patches_populate;
}

// Both redirects, from one init call. mcControlConfigMgr::Load takes the manager
// in a0 - 001A0F58 is `move $a0, $v0` - so it is forwarded rather than called
// with whatever happened to be there.
extern "C" void patches_install(mc3_u32 a0)
{
    pm_state *const s = st();
    MC3_CALL1(void, INIT_ORIG, mc3_u32)(a0);

    volatile mc3_u32 *const mode = (volatile mc3_u32 *)MODE_VTABLE_SLOT;
    const mc3_u32 current = *mode;
    if (valid_pointer(current) && current != mode_populate_fn()) {
        s->prev_mode_populate = current;
        *mode = mode_populate_fn();
    }

    volatile mc3_u32 *const video = (volatile mc3_u32 *)VIDEO_VTABLE_SLOT;
    if (*video == VIDEO_POPULATE) {
        *video = patches_populate_fn();
        s->taken_video = 1;
    }

    MC3_CALL1(void, FLUSH_CACHE, int)(0);
    MC3_CALL1(void, FLUSH_CACHE, int)(2);
}

MC3_HOOK(INIT_SITE, patches_install);
