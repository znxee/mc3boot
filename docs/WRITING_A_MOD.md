# Writing a mod

A mod is one directory with one `.cpp` and the linker script copied beside it.
It links against nothing — no libc, no runtime, no startup code — and reaches the
game by absolute address. That is what makes a 3 KB file out of it.

```
mods/my_mod/
    my_mod.cpp
    mod.ld          <- copy this from any existing mod, unchanged
```

Build it:

```bash
sh mods/build_mod.sh my_mod /path/to/your/HostFS/dir
```

Then name it in the `.ini` and boot.

---

## The smallest useful mod

`mods/toplevel_unlock` is 184 bytes and does one thing:

```c
#include "../../payload/mc3_mod.h"

enum {
    SITE = 0x001A2EB4,   // a `jal` in the game
};

extern "C" void my_hook(void)
{
    // your code
}

MC3_HOOK(SITE, my_hook);
```

`MC3_HOOK(addr, fn)` says: **the `jal` at `addr` should call `fn` instead.**
The loader rewrites that one word. Nothing else in the game changes.

A module with no `MC3_HOOK` at all is treated differently — it gets called once
per frame instead. That is the other shape a mod can take.

---

## Hooking

### The site must be a `jal`

A hook replaces a call, so the word at the address has to be one. This matters
for `= shim` in particular: the loader reads the original target out of the
instruction and parks it in the trampoline's slot, so the game keeps working
until your body arrives. There is nothing to fall back to otherwise, and the
loader refuses the site rather than guessing.

### Your hook replaces the call — chain it yourself

If the game was calling something useful, **you** have to call it:

```c
enum {
    SITE = 0x001A0F5C,
    ORIG = 0x004BAC28,   // what that `jal` pointed at
};

extern "C" void my_hook(mc3_u32 a0)
{
    MC3_CALL1(void, ORIG, mc3_u32)(a0);   // first the original
    /* then yours */
}
```

Decode the original target from the executable — it is in the instruction:

```python
w = struct.unpack_from('<I', elf, va - 0x001A0000 + 0x80)[0]
target = ((w & 0x03FFFFFF) << 2) | (va & 0xF0000000)
```

### Arguments arrive as the game left them

A hook is entered with the registers the original callee would have seen. If the
delay slot sets `a0`, your handler receives it — declare it and pass it on.
Getting this wrong means calling the original with garbage.

### Two mods cannot own the same site

The second install silently replaces the first. If you need to share, redirect a
**vtable slot** instead and chain: save what the slot held, install yours, call
the saved one first. `patches_menu` and `proper_widescreen_menu` share the
top-level menu that way, and installing later means appearing lower.

---

## Calling into the game

```c
MC3_CALL (ret, addr)()                  // no arguments
MC3_CALL1(ret, addr, t1)(a)
MC3_CALL2(ret, addr, t1, t2)(a, b)
// ... up to MC3_CALL6
```

Addresses are fixed for a given executable, so this is just a typed jump. There
is no symbol table to look anything up in.

Five and six arguments work because the ABI here is n32: the first **eight**
integer arguments go in registers, `a0`-`a3` then `t0`-`t3`.

> **Floats are the exception.** The game passes them in `f12`-`f15` with `this`
> in `a0`, which is the o32 numbering. A C function compiled n32 looks for them
> elsewhere and quietly reads the wrong one. If a hook takes floats, write the
> trampoline in assembly and pin the registers — `draw_distance` does this.

---

## Memory

```c
void *p = mc3_alloc(size);
mc3_free(p);
```

This is the game's own `operator new`, the one the executable calls in 1333
places. What it hands out it will not hand to anything else, which is why
deferred modules live there rather than in a range carved out before boot —
that was tried, and the asset loader placed a `.pck` on top of a module.

---

## Printing something

There is no console. One byte written to `0x1000F180` appears in the PCSX2 log,
and that is the whole mechanism:

```c
#include "../../payload/mc3_sio.h"

mc3_sio_mark(77, 89, 77, 79, value);   // prints "MYMO <value>"
```

It needs no debugger, no save state and no action from whoever is watching. A
module can report while the game is still booting.

Note the character **codes**. That is not an affectation — see below.

---

## THE TRAP: `LO16 sem HI16`

Sooner or later `mc3_mkmod` will refuse your build with:

```
LO16 sem HI16 em +164: um `lui` compartilhado por varios acessos.
Ponha a base atras de um acessor noinline.
```

### What is happening

MIPS builds a 32-bit address from two instructions: a `lui` with the high half
and something with the low half. GCC is allowed to emit **one `lui` shared by
several low halves** when the addresses are close together. The module format
records relocations as pairs, so a low half whose high half belongs to a
different access cannot be relocated, and the packer refuses rather than emit a
module that writes to a wrong address at run time.

### What triggers it

Measured, each one separately:

* several loose global variables
* a `static` local inside a function
* a `const` array declared inside a function, which lands in `.rodata`
* **string literals** — every one is another base
* float literals that do not fit a `lui` immediate, which GCC puts in `.rodata`

### The fix: one data base, behind one `noinline` accessor

```c
struct my_state {
    mc3_u32 counter;
    mc3_u16 label[32];
};
static my_state g_state;

static __attribute__((noinline)) my_state *st(void) { return &g_state; }
```

Every global your module has goes **inside that one struct**, reached through
`st()`. One base, one relocation pair, no sharing.

### And build strings numerically

```c
/* not "Patches" - that literal is another data base */
mc3_u16 *t = st()->label;
int n = 0;
t[n++] = 80; t[n++] = 97; t[n++] = 116; t[n++] = 99;   /* Patc */
t[n++] = 104; t[n++] = 101; t[n++] = 115;              /* hes  */
t[n] = 0;
```

Ugly, and it is the difference between a module that builds and one that does
not. The same rule is why `mc3_sio.h` takes character codes.

### A worked example of how quiet this is

`draw_distance` built for weeks. Adding **one** store raised register pressure
enough that GCC moved three float constants into `.rodata` and emitted their
`lui` pairs out of order — and the reported offset pointed at a float load, not
at the line that had been added. The fix was to express those constants as
integer bit patterns.

> **Corollary:** taking the address of a *function* is also a HI16/LO16 pair.
> If you need one, put it behind its own `noinline` accessor too.

---

## Calling a function in another mod

There is no symbol table and no dynamic linker: every `.mod` is relocated to
wherever it lands, so you cannot name another module's function at build time.
What both sides *can* agree on is a number.

```c
#include "../../payload/mc3_registry.h"

/* in the provider */
extern "C" mc3_u32 my_answer(void) { return 42; }
mc3_export(MC3_ID('D','E','M','O'), (void *)&my_answer);

/* in the consumer */
typedef mc3_u32 (*answer_fn)(void);
answer_fn f = (answer_fn)mc3_import(MC3_ID('D','E','M','O'));
if (f) use(f());
```

`mods/registry_provider` and `mods/registry_demo` are that pair, kept as a
working example.

**The id is four characters, not a string, and that is not cosmetic.** A string
literal is a data base — see the trap above. Character literals are plain
integer constants, so `MC3_ID` folds at compile time and adds nothing to the
module.

The table is allocated by `mc3boot` before any module runs, so neither side owns
it and there is no ordering question about the table itself.

### Load order is the real constraint

Only about who has filled it in yet. A module can import only what has already
been exported, and what has run by a given moment depends on the `.ini`: `= 1`
from boot, `shim` from `Main`'s init, `defer` from the first frame. **A hooked
module exports nothing until one of its hooks fires**, because that is the only
time its code runs.

So **import at the point of use, not at load**, and cache the result only once
it comes back non-null. A null is a normal answer.

That trap is not hypothetical — it cost a test run here. The first provider was
`draw_distance`, whose hooks are the race camera and the Camera Options screen;
a headless boot touches neither, so it published nothing and the consumer
correctly found nothing. The example pair is hookless for that reason.

Nothing here is type checked. An id whose signature the two sides disagree about
is a crash.

## Reading `[boot]` from the `.ini`

The same `.mod` file, told at boot time what to do instead of hard-coding it:

```ini
[boot]
city = detroit
```

```c
#include "../../payload/mc3_bootargs.h"

const char *city = mc3_bootarg(MC3_ID('c','i','t','y'));
if (city) { /* resolve it against whatever the mod cares about */ }
```

`mc3_bootarg` returns `NULL` when the key was never set — the ordinary case
when nobody wrote a `[boot]` section — so always keep a compiled-in default to
fall back to, the way `mods/city_force` falls back to a fixed city index when
`[boot] city` is absent or names something that does not resolve.

The key is `MC3_ID`, the exact macro from the registry section above, reused
rather than duplicated: an `.ini` key is folded into the same four bytes by
keeping only its first four characters, entirely on `mc3boot.c`'s side, which
has a real libc and can afford `strncmp` freely. The `.mod` reading it back
never touches a string literal of its own, so it never becomes a second data
base — only *authoring* one does.

### The bug this shape avoids finding twice

`city_force` resolves `[boot] city`'s text against the live city table by
walking it and calling the game's own `strcasecmp` on each record's name
pointer — and the first version of that walk used the same address-sanity
guard several mods in this project use everywhere else:

```c
static int sensato(mc3_u32 p) { return p >= 0x00100000u && p < 0x02000000u && (p & 3u) == 0u; }
```

That guard is right for a table base or an object pointer — both are real
allocations, genuinely 4-aligned. It is wrong for a **name** pointer: city
names live packed back-to-back in one string table (`"sd\0atlanta\0detroit\0..."`),
so every name after the first lands on whatever byte follows the previous
name's NUL — measured, `"atlanta"` sat at an address ending in `...4D`, not
4-aligned at all. The alignment check silently rejected it and ended the whole
search on the second entry, before it ever reached `"detroit"`.

The fix is a second guard with the range check only, used specifically for
string pointers:

```c
static int name_ok(mc3_u32 p) { return p >= 0x00100000u && p < 0x02000000u; }
```

Worth remembering if you copy `sensato()`-style guards into a new mod, as
several already have: decide separately, for each pointer you are about to
validate, whether it actually has to be aligned.

## Text on screen

The game's font is sixteen bits per character. An 8-bit string draws as garbage.
Widen as you copy:

```c
for (const char *p = name; *p && n < limit; ++p)
    out[n++] = (mc3_u16)(unsigned char)*p;
```

---

## Patching code at run time

Write the word, then flush — the R5900 will otherwise keep running the old
instruction out of its instruction cache, which looks exactly like the patch not
having been applied:

```c
*(volatile mc3_u32 *)addr = new_word;
MC3_CALL1(void, 0x00546C20, int)(0);   // FlushCache(0)
MC3_CALL1(void, 0x00546C20, int)(2);   // FlushCache(2)
```

---

## Guard everything you did not write

An address that reads as something unexpected means this is not the executable
your addresses were measured against. Check before writing:

```c
volatile mc3_u32 *const slot = (volatile mc3_u32 *)VTABLE_SLOT;
if (*slot == EXPECTED_ORIGINAL)
    *slot = (mc3_u32)&my_handler;
```

Same for pointers you follow out of the game:

```c
static int valid_pointer(mc3_u32 p)
{
    return p >= 0x00100000u && p < 0x02000000u;
}
```

---

## Measure before you build on it

The habit that has paid for itself most in this project: when something is
*probably* true, spend one small module proving it rather than an afternoon
building on it.

`mods/init_probe` exists for exactly that. It answered "can a module be loaded
this early?" by trying to allocate and open a file at that point and reporting
the result over SIO — and the answer is what the whole `shim` mechanism rests
on. It is kept in the repository as an example of the shape.

Counters are not proof of behaviour, either. "12 hooks installed" says the
loader did its half, not that your mod ran. Report something only your code
could have produced.
