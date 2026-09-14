# The generated headers

Three headers are generated and committed, so a fresh clone builds with no setup:

| file | generated from | contains |
|---|---|---|
| `boot/addresses.h` | the game executable | entry point, segment bounds, the cave map |
| `payload/patches.h` | the `.pnach` files in `patches/` | the patch groups, as C++ |
| `boot/bootstrap.h` | the HostFS `.pnach` | the writes that make the game read loose files |

They contain **addresses and patch values, never game code**. Committing them is
a convenience, not a design constraint — regenerate them freely.

## Retargeting a different build of the game

Every address in this project was measured against **SLUS-21355**, the NTSC-U
release. For another region or build:

```bash
cd boot
python gera_addresses.py /path/to/YOUR_EXECUTABLE.ELF
```

That reads the entry point and segment layout straight out of the ELF, so
nothing about it is copied by hand.

The patch groups are a different matter: their addresses were found by hand
against this executable and will be wrong elsewhere. So will every address a mod
hard-codes. Retargeting the loader is mechanical; retargeting the mods is
research.

## Adding patch groups, including other people's

`patches.h` is generated from a folder of PCSX2 `.pnach` files:

```bash
python mc3_pnach.py --dir /path/to/your/pcsx2/cheats \
                    --elf /path/to/SLUS_213.55.ELF \
                    --to-cpp payload/patches.h
```

Anything you point it at appears in the table, in the `.ini` under `[patches]`,
and in the in-game patches menu — grouped, and with mutually exclusive
alternatives detected automatically, because the rule behind that is simply
"these write the same address".

This is how to get back the patch work that is deliberately **not** in this
repository: the 60 fps group by `robertesaum1`, the widescreen hacks by
`SuperType1/Remco` and by `Arapapa`. Get them from their authors, drop them in
your cheats folder, regenerate.

## Regenerating the `.ini`

```bash
python boot/gera_ini.py mc3boot.ini
```

Writes every group the table knows about, commented, with the state the build
currently has — so a freshly generated `.ini` reproduces exactly the build you
have. Editing it is then a change rather than a guess.
