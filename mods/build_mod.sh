#!/bin/sh
# Builds one standalone mod into a .mod the loader can place anywhere.
#
#   sh build_mod.sh toplevel_unlock [HostFS dir]
#
# Every build has two outputs by convention:
#   ../../output/mods/NAME.mod   shared generated artefact
#   HOSTFS/NAME.mod              copy used by the running game
#
# A mod is one directory with one .cpp and the linker script copied beside it.
# It links against nothing - no libc, no runtime - and reaches the game by
# absolute address, which is what makes a 3 KB file out of it.
set -e
NAME="${1:?usage: build_mod.sh <mod directory name> [HostFS dir]}"
HERE="$(dirname "$0")"
DIR="$HERE/$NAME"
OUTDIR="$HERE/../../output/mods"
HOSTFS="${2:-${MC3_HOSTFS:-/y/MC3HostFS}}"

[ -d "$DIR" ] || { echo "no such mod: $DIR" >&2; exit 1; }
[ -d "$HOSTFS" ] || { echo "HostFS directory not found: $HOSTFS" >&2; exit 1; }
mkdir -p "$OUTDIR"

for P in mips64r5900el-ps2-elf ee; do
    if command -v "$P-g++" >/dev/null 2>&1; then PREFIX="$P"; break; fi
done
[ -n "${PREFIX:-}" ] || { echo "ps2dev toolchain not in PATH" >&2; exit 1; }

# MSYS2 installations differ here: some expose `python`, others only
# `python3`.  The packer is Python 3 code, so accept either spelling instead of
# making a successful C++ build fail at the final packaging step.
if command -v python >/dev/null 2>&1; then
    PYTHON=python
elif command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
else
    echo "Python 3 not in PATH" >&2
    exit 1
fi

# Same flags as the payload: freestanding, no PIC, nothing in .sdata. -G0 in
# particular matters - injected code has no $gp of its own to reach a small
# data area through.
"$PREFIX-g++" -c "$DIR/$NAME.cpp" -o "$DIR/$NAME.o" \
    -O2 -G0 -mno-abicalls -nostdlib -ffreestanding \
    -fno-exceptions -fno-rtti -fno-builtin -Wall

# -q keeps the relocations, which is the whole point: the loader replays them
# against whatever address it puts the module at.
"$PREFIX-ld" -q --entry=0 -T "$DIR/mod.ld" "$DIR/$NAME.o" -o "$DIR/$NAME.elf"
"$PYTHON" "$HERE/../mc3_mkmod.py" "$DIR/$NAME.elf" "$OUTDIR/$NAME.mod"
cp "$OUTDIR/$NAME.mod" "$HOSTFS/$NAME.mod"
echo "   HostFS: $HOSTFS/$NAME.mod"
