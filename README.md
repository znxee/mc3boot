# mc3boot — a mod loader for Midnight Club 3: DUB Edition (PS2)

Write a mod in C++, drop the file next to the game, name it in an `.ini`. No
recompiling the loader, no cheat-engine codes, no patched ISO.

```c
#include "../../payload/mc3_mod.h"

extern "C" void my_hook(void) { /* your code, running inside the game */ }

MC3_HOOK(0x001A2EB4, my_hook);
```

```ini
[mods]
my_mod.mod = shim
```

That is the whole surface. The rest of this file is what is behind it.

**This repository contains no game code, no game assets and no executable.** You
need your own legally obtained copy of Midnight Club 3: DUB Edition (SLUS-21355).

---

## Why a chain loader and not a cheat file

A `.pnach` or an Action Replay code can only change a constant. That covers a
lot, but not anything depending on a value that **only exists at run time**.

The case that motivated this: pedestrian animation advances by a rate in *frames
per tick*, with no delta time. At 60 fps they walk twice too fast. You can fix
that with a constant by halving it — but then it is wrong at 30, at 45, at 120.
A mod computes `rate = base * dt * 30` and is right at any frame rate. Measured
at 120.47 Hz: 14 of 14 pedestrians at exactly the expected ratio.

Running code needs the code to be *in memory* before the game starts, and that
is the part a cheat file cannot do: at the end of the game's `crt0` there is no
file system yet, so an external payload simply is not there to be read.

`mc3boot` solves it by running **instead of** the game and then handing the
machine over:

1. read the mods — a full file system exists here, we are still a normal program
2. read the game executable into place
3. write the loader, the modules and the hooks into the image
4. apply the constant patch groups
5. flush the caches, reset the IOP, jump to the game's entry point

Everything a mod needs is in RAM before the game's first instruction. It also
retires the cheat file entirely: no PCSX2 requirement, no code limits, no CRC
matching, and the same binary on an emulator and on real hardware.

---

## Requirements

| | |
|---|---|
| **ps2dev toolchain** | `mips64r5900el-ps2-elf-gcc` and ps2sdk. Needed to build the loader and any mod. |
| **Python 3** | Builds the `.mod` files and generates the address headers. No packages required. |
| **PCSX2 2.x**, or a real PS2 | PCSX2 needs HostFS, or you can boot the ELF from a disc image. Real hardware runs it through OPL. |
| **Your own copy of the game** | SLUS-21355. Not included, not linked, not distributed. |

### Installing the toolchain

The maintained installer is [ps2dev/ps2toolchain](https://github.com/ps2dev/ps2toolchain).
On MSYS2 the result lands in `/opt/ps2dev`, and two things have to be on `PATH`
for it to run — the compiler itself and the MSYS2 runtime it links against:

```bash
export PATH="/c/msys64/opt/ps2dev/ee/bin:/c/msys64/usr/bin:$PATH"
export PS2DEV=/c/msys64/opt/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
```

Leaving out `/c/msys64/usr/bin` produces
`cc1plus.exe: error while loading shared libraries: msys-mpc-3.dll`, which looks
like a broken toolchain and is only a missing path.

Verify with:

```bash
mips64r5900el-ps2-elf-g++ --version
```

---

## Quick start

```bash
# 1. point the address generator at your executable
cd boot
python gera_addresses.py /path/to/SLUS_213.55.ELF

# 2. build the loader
make PS2SDK=$PS2SDK PS2DEV=$PS2DEV

# 3. build a mod
cd ..
sh mods/build_mod.sh draw_distance /path/to/your/HostFS/dir
```

Then put `mc3boot.elf`, the `.mod` files and an `mc3boot.ini` in the same place
as the game, and **boot `mc3boot.elf` instead of the game**.

Generate a starting `.ini` with every option listed and commented:

```bash
python boot/gera_ini.py mc3boot.ini
```

---

## The `.ini`

```ini
[mods]
core.mod = bootstrap
draw_distance.mod = shim
patches_menu.mod = shim
my_experiment.mod = 0

[patches]
Video/Widescreen 16:9 = 1
Cidade/Orcamento para 60 fps = 1
```

Modules load **in the order written**. The value decides *where the module
lives*, and that matters because the space inside the game image is finite:

| value | where the body goes | hooks live from | costs cave |
|---|---|---|---|
| `1` | the cave, inside the game image | boot | its whole size |
| `defer` | the game's heap, loaded on the first frame | first frame | nothing |
| `shim` | the game's heap, loaded during `Main`'s init | `001A0F28` | 24 bytes per hook site |
| `defer_once` / `shim_once` | as above, entry called once instead of per frame | | |
| `bootstrap` | the cave, plus the per-frame callback a hooked module normally gives up | boot | its whole size |
| `0` | not loaded | | |

**`shim` is what a normal mod wants.** The body goes on the heap where size stops
mattering, and each hook site is claimed at boot by a 24-byte trampoline so
nothing early is missed. `bootstrap` exists for exactly one module — `core` —
and nothing else should use it.

The same `.mod` file works under every value. Only the line changes.

### Why `shim` is not just `defer`

A trampoline claiming the site early is **not** the same as being able to answer
early. Until the body exists, the trampoline's slot still holds the original
call target, so a site that fires once during boot is missed exactly as it would
be with a plain `defer`. That is why a `shim` module's body is loaded during
`Main`'s init rather than on the first frame — measured to be possible: at
`001A0F28` the game's allocator returns real blocks and its file API opens a file
and reports its exact size. `mods/init_probe` is the measurement, kept as an
example.

`defer` deliberately keeps loading on the first frame. Loading everything early
would move the ground under mods written against that contract.

### `[boot]` — configuration a mod reads instead of hard-codes

```ini
[boot]
city = detroit
```

Testing one specific thing — boot straight into a given city, a given car once
that mod exists too — used to mean a constant baked into the `.cpp`, a rebuild,
and a copy to HostFS for every change. `[boot]` lines are free-form `key =
value` pairs a module can ask for at run time:

```c
#include "../../payload/mc3_bootargs.h"

const char *city = mc3_bootarg(MC3_ID('c','i','t','y'));
if (city) { /* use it */ }   // NULL: the key was not set this boot
```

The key is folded into the same four-character id `mc3_registry.h` already
defines for calling between mods — a string literal in a `.mod` is a data base
and `mc3_mkmod` refuses a build where GCC shares one `lui` across several (see
`docs/WRITING_A_MOD.md`), so a module never compares strings; only the loader,
which has a real libc, touches the key text. `mods/city_force` is the worked
example: it reads `[boot] city`, resolves the name against the game's own live
city table, and falls back to a compiled-in default when the key is absent or
does not resolve to anything registered.

### `[boot]` also reaches the game's own argument parser, unmodified

The retail executable still has a complete, functional `datArgParser`:
`Init(argc, argv)`, `Get(key)`, `SaveToArchive`, `RestoreFromArchive`, `Kill`,
all named at fixed addresses and all called from `main()` before the game
proper starts. What it does not have any more is anything feeding it — the
function that would build `argv` from a response file on disk disassembles to
an empty stub in this build, and nothing in the executable's own data calls
`Get()` with any flag name. The parser survived; its data source did not.

`mods/native_bootargs` reconnects the two without touching either: it takes
the exact same `[boot]` lines (untruncated, unlike the four-character keys
above — `datArgParser` hashes the full string) and calls `Init` a second time
with a synthetic `argv` built from them, once, on the first frame. From then
on any mod can call the game's own `Get("key")` and get a real answer, driven
by the `.ini` instead of a rebuild.

```ini
[boot]
city = detroit
```

```c
#include "../../payload/mc3_native_args.h"

mc3_u32 argv;
const mc3_u32 argc = mc3_native_argv(&argv);   // 0 if [boot] set nothing
// feed argc/argv to datArgParser::Init once; see mods/native_bootargs
```

This only revives the parser — it does not reimplement what any flag *does*.
That still means writing an ordinary hook, the same as everything else here.

### `[boot]` flags that already had somewhere to land

Three did: `mods/race_bootargs` reads `time`, `weather` and `racetype` and
calls the game's own setters —
`mcRaceConfig::SetTOD`/`SetWeather`/`SetRaceType` — the exact three functions
a race's own text file already drives. Each validates its string against a
fixed table (`dawn`/`midnight`/`dusk`, `clear`/`cloudy`/`rainy`, and 23 race
types from `roam` to `ordered_track`) and ignores anything else, so there was
no guard to write by hand:

```ini
[boot]
time = dusk
weather = rainy
racetype = trial
```

Two flags from the same wishlist did not have anywhere to land, and why is
worth keeping rather than guessing again later: a race's text format still
accepts a `ForcedCar` key, but disassembly shows it reads the value and never
uses it — vestigial, like `ResponseFile` above. And there is no opponent
*count* field to set at all; opponents come from a list `mcRaceBase` parses
out of the race file, which would need intercepting that parse, not writing
an int.

---

## The space budget

Modules that live in the cave share **10072 bytes** at `0061D100..0061F858`
inside the game image. Running out of it produces an infinite load before the
front end, which says nothing about the cause — so the loader reports the real
figures over the PS2's SIO port, where they land in the PCSX2 log:

```
MC3ARENA base=0061D100 end=0061F858 mods_top=0061DFD0 shims_bot=0061F5A0
         used=01188 free=015D0 shims=00D orig=030 hooks=00E mods=002
```

Modules grow up from `base` and shims grow down from `end`, so `free` is what is
genuinely left between them. `core` itself then reports what the heap half did:

```
EARL 00000006      modules loaded during Main init
DEFR 0000000C      total loaded, including the first-frame pass
SHIM 0000000D      hooks routed through a shim slot
```

Do not add up `.mod` file sizes to estimate this — a file carries a 32-byte
header and its relocations on top of the code. The `MC3ARENA` line is the
number.

---

## Writing a mod

One directory, one `.cpp`, one linker script copied beside it. It links against
nothing — no libc, no runtime — and reaches the game by absolute address, which
is what makes a 3 KB file out of it.

See **[docs/WRITING_A_MOD.md](docs/WRITING_A_MOD.md)** for the full walk-through,
including the one trap that will otherwise cost you an afternoon (`LO16 sem
HI16`).

### The examples, smallest first

| mod | what it demonstrates |
|---|---|
| `toplevel_unlock` | 184 bytes. The minimum: one hook, one write. |
| `frontend_no_timeout` | A hookless per-frame module. |
| `debug_draw` | Turning on drawing the game already has but never calls. |
| `init_probe` | Measuring whether something is possible before building on it. |
| `menu_row` | Adding a row to the top-level menu. |
| `draw_distance` | A real option: a menu row, a saved setting, and an assembly trampoline that rewrites its own `lui` immediate. |
| `patches_menu` | Taking over a whole menu screen the game builds and never uses. |
| `registry_provider` + `registry_demo` | One mod calling a function that lives in another. |
| `city_force` | Reading `[boot]` from the `.ini` instead of a compiled-in constant. |
| `native_bootargs` | Feeding `[boot]` into the game's own, still-functional `datArgParser`. |
| `race_bootargs` | `[boot] time`/`weather`/`racetype`, applied through the game's own setters. |
| `core` | The module that loads the other modules. Read it last. |

---

## How a `.mod` is put together

`mc3_mkmod.py` turns a linked ELF into a relocatable module: a header, the code,
and a list of the places whose contents are absolute addresses. Placing one is a
copy followed by walking that list and adding the delta.

That relocation is what lets someone add a mod by dropping a file in. Without it
every mod would need a fixed address, and two mods by two authors would collide
with no way to tell. The format is in
**[docs/MOD_FORMAT.md](docs/MOD_FORMAT.md)**.

---

## Generated files

`boot/addresses.h`, `payload/patches.h` and `boot/bootstrap.h` are generated and
committed, so a fresh clone builds without setup. They contain addresses and
patch values — never game code. To retarget them at a different build of the
game, see **[docs/GENERATED.md](docs/GENERATED.md)**.

---

## Limits

* **One executable.** Every address here was measured against SLUS-21355 (the
  NTSC-U release). Another region or another build needs the headers
  regenerated, and mods hard-code addresses of their own besides.
* **The cave is 10072 bytes**, and that is a hard ceiling for `= 1` modules.
  Use `shim`.
* **A mod is trusted code** running with full access to the machine. There is no
  sandbox. Read what you run.
* **This is reverse engineering of a twenty-year-old game**, done by measurement.
  Some of what the comments assert was measured on one build, in one scene, and
  the comments try to say which.

---

## Credits and licence

The loader, the module format and the mods in this repository are the work of
this project. The patch groups in `patches/` are its own as well.

Patch work by other people is **not** included here. If you want the 60 fps
group by `robertesaum1`, the widescreen hack by `SuperType1/Remco` or by
`Arapapa`, get them from their authors and point the generator at your own cheat
folder — `docs/GENERATED.md` explains how. They will then appear in the in-game
patches menu alongside everything else.

Licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

Midnight Club 3: DUB Edition is a trademark of its respective owners. This
project is unaffiliated with Rockstar Games, Rockstar San Diego or Take-Two
Interactive, and is not endorsed by them.
