#!/bin/sh
# Builds the payload with the ps2dev toolchain, in both shapes:
#
#   mc3mod.bin   linked at the cave address, for the pnach routes
#   peds.mod     relocatable, for mc3boot's [mods] list
#
# The prefix changed over releases: current ones install
# `mips64r5900el-ps2-elf-gcc`, not `ee-gcc`. Both are detected.
#
#   -G0            nothing in .sdata/.sbss: injected code has no $gp of its own
#   -mno-abicalls  no PIC; the binary is linked at a fixed address
#   -nostdlib      no runtime, no crt0; the entry is payload_main
#   -fno-exceptions -fno-rtti   there is no C++ runtime to support them
set -e
OUT="${1:-/y/MC3HostFS/mc3mod.bin}"
HERE="$(dirname "$0")"

for P in mips64r5900el-ps2-elf ee; do
    if command -v "$P-g++" >/dev/null 2>&1; then PREFIX="$P"; break; fi
done
if [ -z "${PREFIX:-}" ]; then
    echo "ps2dev toolchain not found in PATH." >&2
    echo "  export PS2DEV=/opt/ps2dev" >&2
    echo "  export PATH=\$PATH:\$PS2DEV/bin:\$PS2DEV/ee/bin" >&2
    echo "On MSYS2 use the MSYS shell, not MINGW64 - see ../README.md." >&2
    exit 1
fi
echo "toolchain: $PREFIX  ($($PREFIX-g++ --version | head -1))"

# patches.h is generated from the real .pnach files. Regenerate it whenever they
# change - it is the single source of truth for every address:
#   python ../../mc3_pnach.py --dir <cheats> --elf <elf> --to-cpp patches.h
if [ ! -f "$HERE/patches.h" ]; then
    echo "patches.h missing - generate it with mc3_pnach.py --to-cpp" >&2
    exit 1
fi

# The object list is carried in the positional parameters, not in a string. The
# toolkit directory is allowed to have a space in its name - it has one - and an
# unquoted "$OBJS" would split on it. $1 is read into $OUT above, before this
# resets the list, so nothing is lost.
set --
for SRC in payload.cpp patches.cpp; do
    "$PREFIX-g++" -c "$HERE/$SRC" -o "$HERE/${SRC%.cpp}.o" \
        -O2 -G0 -mno-abicalls -nostdlib -ffreestanding \
        -fno-exceptions -fno-rtti -fno-builtin -Wall
    set -- "$@" "$HERE/${SRC%.cpp}.o"
done

# payload.o FIRST: its .text.start has to land at BASE+4.
"$PREFIX-ld" -T "$HERE/payload.ld" "$@" -o "$HERE/payload.elf"
"$PREFIX-objcopy" -O binary "$HERE/payload.elf" "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"

# The relocatable build, for the chain loader's [mods] list. Same objects, a
# linker script based at 0, and `-q` so the relocations survive - that is what
# lets the loader place it anywhere, which is what lets someone add a mod by
# copying a file instead of installing a compiler.
"$PREFIX-ld" -q -T "$HERE/payload_rel.ld" "$@" -o "$HERE/payload_rel.elf"
python "$HERE/../mc3_mkmod.py" "$HERE/payload_rel.elf" "$(dirname "$OUT")/peds.mod"

echo
# The entry must sit at BASE+4, right after the signature. Printing it here means
# a linker-script change that moves it is visible immediately, not three tests
# later.
echo "--- entry point (must be the address the loader jumps to) ---"
"$PREFIX-nm" "$HERE/payload.elf" | grep -iE "payload_main|g_signature"
"$PREFIX-objdump" -d "$HERE/payload.elf" | sed -n '/<payload_main>:/,+6p'
