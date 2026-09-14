# The `.mod` format

Produced by `mc3_mkmod.py` from a linked ELF, placed by `mc3boot` before the game
starts and by `core.mod` afterwards. Both use the same code — `payload/mc3_modfmt.h`
— because having the parser in two files is how the two drift apart.

## Header, 32 bytes, little endian

| offset | field |
|---|---|
| `+0`  | `'MC3M'` (`0x4D43334D`) |
| `+4`  | version, currently `2` |
| `+8`  | entry offset from the module base |
| `+12` | code size |
| `+16` | relocation count |
| `+20` | link base — what it was linked at; the loader computes the delta |
| `+24` | hook table offset from the module base, `0` if none |
| `+28` | hook count |
| `+32` | the code |
| after the code | the relocations, 12 bytes each |

## Relocations, `[type][off1][off2]`

| type | meaning |
|---|---|
| `1` | `word32` — `off1` is the offset of a 32-bit absolute word |
| `2` | `jal26` — `off1` is a `j`/`jal` whose 26-bit target needs the delta |
| `3` | `hi_lo` — `off1` is the `lui`, `off2` its paired low half |

Placing a module is a copy followed by walking this list and adding
`dest - link_base`.

Type 3 is the awkward one and it is resolved at **build** time rather than here:
pairing a `%hi` with its `%lo` needs the compiler's knowledge, and a `%lo` whose
`%hi` is shared with another access cannot be paired at all. That is the
`LO16 sem HI16` error, and `docs/WRITING_A_MOD.md` explains how to avoid earning
it.

When the pair is added back together the low half is **sign extended**, so the
high half has to carry the borrow:

```c
value = ((hi & 0xFFFF) << 16) + (int)(short)(lo & 0xFFFF) + delta;
new_lo = value & 0xFFFF;
new_hi = ((value - (int)(short)new_lo) >> 16) & 0xFFFF;
```

## The hook table

8 bytes per entry: the address of a `jal` in the game, then the handler to call
instead.

The handler is an ordinary absolute address, so the linker emits an `R_MIPS_32`
for it and the relocation pass above fixes it like anything else — the table
needs no special case. It is therefore read **after** relocation.

The target address beside it carries no relocation: it is a constant naming a
place in the game, not in the module. That is what lets `mc3boot` read the hook
sites out of a `.mod` file at boot **without placing the module** — which is how
`= shim` claims a site while the body is still on disk.

## Reading it

```
python mc3_mkmod.py --help
```
