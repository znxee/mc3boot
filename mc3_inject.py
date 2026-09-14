"""
mc3_inject.py - bootstrap your own C++ into Midnight Club 3 (PS2, SLUS-21355)

Writing a whole mod as memory patches is one line per 32-bit word: a kilobyte of
code is 256 lines, and every rebuild rewrites the file. This turns that around.
The patch file carries only a LOADER - 61 instructions - and everything else can
live in a loose binary that the game reads at run time through its own Stream
API. Rebuild the payload, restart, done; the patch file never changes again.

Three targets, one loader:

    pnach            PCSX2 + HostFS. Payload in a loose file next to the ELF.
    pnach --embed    PCSX2 from an ISO. Payload carried inside the patch file.
    cht              Real PS2 through OPL, payload as a file in the image.
    cht  --embed     Same, payload inline - ~720 codes, over most OPL limits.

    python mc3_inject.py build --out Y:/MC3HostFS/mc3mod.bin
    python mc3_inject.py pnach --out ".../SLUS-21355_60A42FF5_modloader.pnach"
    python mc3_inject.py pnach --embed payload/mc3mod.bin --out ...
    python mc3_inject.py cht   --embed payload/mc3mod.bin --out CHT/SLUS_213.55.cht
    python mc3_inject.py all   --embed payload/mc3mod.bin --outdir dist/
    python mc3_inject.py state "savestate.p2s"

See README.md for what each route costs and why `cht` requires `--embed`.
"""

import argparse
import io
import os
import struct

# --------------------------------------------------------------------------
#  Fixed memory map
# --------------------------------------------------------------------------
#  The cave lives INSIDE the ELF image: a 12288-byte run of zeros at 0x61C858,
#  in a PROGBITS section, so it is loaded from the file. Confirmed still all
#  zeros in a gameplay savestate, meaning the game never writes there.
#
#  The first attempt used 0x00090000, "free" RAM between the EE kernel and the
#  ELF. It is free in a mid-gameplay savestate, but the boot ZEROES that range
#  after the patch writes - the hooks then jumped into NOPs, ran on, and landed
#  in garbage (PC = 0xCDCDCDCD). Memory outside the image does not survive boot.
CAVE     = 0x0061C858      # the loader, 244 bytes
NAME     = 0x0061C960      # the path string
STATE    = 0x0061C980      # +0 state, +4 stream, +8 size, +12 bytes read
# Public game telemetry for PINE/ReShade. Exactly 48 bytes in the otherwise
# unused tail after STATE; mc3_telemetry.mod owns it and publishes ABI v1.
PUBLIC_TELEM = 0x0061C990  # magic/version/sequence/flags/speed/rpm/gear/vector
PUBLIC_TELEM_MAGIC = 0x4D433354
# Written by the payload's patches.cpp, read back here. Sits in the gap between
# the state block and the payload; keep the layout in step with mc3_telemetry.
TELEM    = 0x0061C9C0      # magic, groups, writes, changed, flushes, passes, dt
TELEM_MAGIC = 0x4D433350
# Feature flags, written by the chain loader from mc3boot.ini and read by the
# payload. Absent (magic zero) on the pnach routes, where the payload falls back
# to what it was compiled with - so this never breaks an older setup.
CFG      = 0x0061C9F0      # +0 magic 'MC3C', +4 flag bits
CFG_MAGIC = 0x4D433343
# Why a module did not load, recorded by the chain loader so the reason reaches
# a savestate instead of scrolling off the screen.
TRACE    = 0x0061CA60
TRACE_MAGIC = 0x4D433354
TRACE_ERR = {0: 'ok', 1: 'open falhou', 2: 'curto demais', 3: 'magic errado',
             4: 'versao errada', 5: 'truncado', 6: 'tabela cheia',
             7: 'nao cabe', 8: 'relocacao desconhecida'}
PAYLOAD  = 0x0061D000      # the payload
MAX_SIZE = 0x00002800      # 10240 bytes, with room to the end of the block

#  MULTI-MODULE LAYOUT
#
#  The cave loader jumps to PAYLOAD+4 and always will - the pnach routes depend
#  on it. So PAYLOAD+4 becomes a DISPATCHER that walks a null-terminated table of
#  entry points, and the modules live behind it. One fixed jump, any number of
#  mods, and nothing about the existing routes changes.
#
#     0061D000  signature
#     0061D004  dispatcher            <- what the cave loader calls
#     0061D050  hook shim header      <- in the gap the dispatcher leaves
#     0061D080  entry table, null terminated
#     0061D100  modules, packed in order    ->        <- hook shims, packed down
#     0061F858  end of the zeroed block in the image
DISPATCH = PAYLOAD + 4
# The dispatcher is 18 words and ends at 0061D04C, so 52 bytes go spare before
# MODTAB. The shim header is 16 of them.
SHIMHDR  = 0x0061D050      # magic, count, base, stride
SHIM_MAGIC = 0x4D433353    # 'MC3S'
MODTAB   = 0x0061D080      # 31 entries plus the terminator
CAVE_END = 0x0061F858      # end of the zeroed block inside the image

#  WHERE THE MODULES GO
#
#  In the cave, inside the game image. Not on the heap, and not in a range carved
#  out of it - that was tried and it is wrong.
#
#  The reasoning for the carve was sound as far as it went: the crt0 passes the
#  heap base to SetupHeap as a plain `lui a0,0x71`, so raising it by one word
#  leaves the range below untouched by that allocator. What it missed is that the
#  ALLOCATOR IS NOT THE ONLY THING THAT PUTS DATA THERE. The asset loader places
#  .pck images from the end of the ELF image onwards regardless, and it walked
#  straight over a module: at the address a hook jumped to sat `CDCD0033
#  736D6952` - "Rims" and the .pck padding pattern - and the game hung before its
#  first frame.
#
#  So: 10 KB inside the image, which nothing has ever been observed to touch, in
#  preference to 256 KB that the game demonstrably reuses. If more room is needed
#  the answer is to find where the asset arena's base comes from, not to assume a
#  range is free because one allocator was moved off it.
#  The list of modules the .ini deferred to run time, left for core.mod to read.
#  Sits in the gap between the boot trace and the dispatcher table.
CORE_TRACE  = 0x0061CAC0   # written by core.mod, read by `state`
DEFER       = 0x0061CAE0
DEFER_MAGIC = 0x4D433344   # 'MC3D'

#  A REPORT WINDOW FOR MODS
#
#  Every fixed address above belongs to one piece of the loader, and a mod that
#  wants to leave a result where a savestate can find it has nowhere to put it.
#  Picking an address out of the game's memory is the mistake that cost a whole
#  boot once already: what looks unused is only unused in the state you looked
#  at. So the last 256 bytes of the defer region become that window, reserved
#  here rather than claimed by whichever mod gets there first.
#
#  It is not owned by any one mod: the magic in word 0 says who wrote it, and
#  `state` prints whatever it recognises. Two mods reporting at once would
#  collide, which is a fair trade for not having to grow the map every time.
#  MAGICS EM USO, para ninguem repetir um por acaso. Cada um marca um bloco
#  que o `state` sabe ler:
#      MC3B mc2_probe   MC3C config     MC3D defer      MC3G city_place
#      MC3K log v2      MC3L place tbl  MC3M .mod       MC3N menu_row
#      MC3P payload     MC3R core       MC3T boot       MC3W widescreen
#      MC3Y city_slot6
MODREPORT   = 0x0061CF00   # 256 bytes, savestate-readable, first word = magic
#  WHO USES WHICH BYTES OF IT
#
#  Keep this table when you add a mod, and COUNT the struct rather than
#  remembering it. city_place was placed at +232 on the belief that menu_row was
#  32 bytes; it had grown to 40, and the two mods silently overwrote each
#  other's report - which reads as a mod that stopped working.
#
#      +0    176   city_slot6.mod   'MC3Y'   header + 6 records x 24
#      +176   24   city_place.mod   'MC3G'
#      +200   40   menu_row.mod     'MC3N'
#      +240   16   proper_widescreen(.mod + _menu.mod)   'MC3W'
DEFER_MAX   = MODREPORT - DEFER       # bytes available for magic+count+paths
WS_REPORT   = MODREPORT + 240

MODBASE  = 0x0061D100
MODEND   = 0x0061F858      # first byte NOT ours: end of the zeroed block
CAVE_END = MODEND

# game functions, from the symbol table in ../simbolos/
STREAM_OPEN  = 0x003991F0
STREAM_READ  = 0x003993A8
STREAM_CLOSE = 0x00399748
STREAM_SIZE  = 0x003997A8
TIMER_UPDATE = 0x00433490

#  The FOUR `jal datTimeManager::Update` sites. Hooking all of them matters: the
#  one in mcGame::Update only runs once the city is up, so the loader's first
#  run - the one that opens a file - would land at the very end of city loading
#  with the streamer busy, and hang. Through the menus the load happens with the
#  file system idle.
#  THE BOOT HOOK.
#
#  The four hooks above all live inside mcGame, so the payload first runs long
#  after the game has booted, read its video config and shown its first loading
#  screen. Some patches have to be in place before that. In a pnach those lines
#  can be `patch=0`; through OPL on real hardware there is no such thing - every
#  code is reapplied every frame and nothing is "on startup". So on hardware this
#  is the only way to get startup semantics at all.
#
#  Where the earliest safe point is, from the crt0 at 0x1A0008:
#
#     0x1A0184  syscall 60 (SetupThread) -> returns the stack top in $v0
#     0x1A0188  daddu sp, v0, zero        <-- $sp becomes valid HERE
#     0x1A01A0  syscall 61 (SetupHeap)
#     0x1A01A4  jal _InitSys
#     0x1A01AC  jal FlushCache
#     0x1A01B4  ei                        <-- the hook goes here
#     0x1A01E4  jal ps2main               -> main -> Main -> everything else
#
#  Anything before 0x1A0188 is out: the entry point zeroes all 32 registers,
#  $sp included, and the stub needs a stack. `ei` at 0x1A01B4 is the last
#  instruction before the game proper starts, it is not in a delay slot, and its
#  own delay slot (`lui v0,0x61`) is harmless to run early. The stub does its
#  work with interrupts still off and issues the `ei` itself on the way out.
EARLY      = 0x0061CA00     # the boot stub, in the free space after TELEM
EARLY_HOOK = 0x001A01B4     # the `ei` at the end of crt0
EARLY_WORD = 0x42000038     # what it replaces

HOOKS = (0x001A3164,   # mcGame::UpdateGarage
         0x001A32C8,   # mcGame::UpdateInGameFrontend
         0x001A353C,   # mcGame::UpdateInGameRaceEditor
         0x001A385C)   # mcGame::Update

#  The HostFS device is `host0:`, with the zero - that is what the game's own
#  strings use ("host0:/dnas.rel"). With `host:` the open returns 0.
#  On real hardware through OPL, USB storage usually shows up as `mass0:`.
PATH  = 'host0:/mc3mod.bin'
# Real PS2: no HostFS. The game's own device is cdrom0: in the `NAME.EXT;1`
# form it already uses for cdrom0:\STREAMS.DAT;1, so the payload can
# just be another file in the rebuilt image.
PATH_ISO = 'cdrom0:\\MC3MOD.BIN;1'
MAGIC = 0x4D433350        # first word of the payload; no match, no jump

# Generated files go to tools/output/, so the toolkit stays source-only.
DEFAULT_OUTDIR = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), 'output', 'modloader')


# --------------------------------------------------------------------------
#  Minimal MIPS assembler: only what the loader uses
# --------------------------------------------------------------------------
R = {'zero': 0, 'at': 1, 'v0': 2, 'v1': 3, 'a0': 4, 'a1': 5, 'a2': 6, 'a3': 7,
     's0': 16, 's1': 17, 's2': 18, 'ra': 31, 't0': 8, 't1': 9, 'sp': 29}


def _i(op, rs, rt, imm):
    return (op << 26) | (R[rs] << 21) | (R[rt] << 16) | (imm & 0xFFFF)


def _r(rs, rt, rd, funct):
    return (R[rs] << 21) | (R[rt] << 16) | (R[rd] << 11) | funct


class Asm(object):
    """Assembles a word list, resolving labels in a second pass.

    The delay slot is NOT filled automatically. Hiding it is the best way to
    write a MIPS bug: a two-instruction macro straddling a `jal` leaves its
    second half running AFTER the call. That happened here once and worked by
    luck, because the constant's low half was zero.
    """

    def __init__(self, base):
        self.base = base
        self.out = []
        self.labels = {}

    def label(self, name):
        self.labels[name] = self.base + 4 * len(self.out)

    def raw(self, w):
        self.out.append(w)

    # --- instructions ----------------------------------------------------
    def nop(self):        self.raw(0)
    def lui(self, rt, i): self.raw(_i(0x0F, 'zero', rt, i))
    def ori(self, rt, rs, i): self.raw(_i(0x0D, rs, rt, i))
    def addiu(self, rt, rs, i): self.raw(_i(0x09, rs, rt, i))
    def li(self, rt, i):  self.raw(_i(0x09, 'zero', rt, i))
    def lw(self, rt, off, base): self.raw(_i(0x23, base, rt, off))
    def sw(self, rt, off, base): self.raw(_i(0x2B, base, rt, off))
    def move(self, rd, rs): self.raw(_r(rs, 'zero', rd, 0x2D))     # daddu
    def jr(self, rs):     self.raw(_r(rs, 'zero', 'zero', 0x08))
    def jal(self, target): self.raw(0x0C000000 | ((target >> 2) & 0x3FFFFFF))
    def j(self, target):  self.raw(0x08000000 | ((target >> 2) & 0x3FFFFFF))
    def sltu(self, rd, rs, rt): self.raw(_r(rs, rt, rd, 0x2B))

    def la(self, rt, addr):
        """A 32-bit address in two steps, with the addiu's sign carry handled."""
        hi, lo = (addr >> 16) & 0xFFFF, addr & 0xFFFF
        if lo & 0x8000:               # the following addiu would read it signed
            hi = (hi + 1) & 0xFFFF
        self.lui(rt, hi)
        self.addiu(rt, rt, lo)

    def _branch(self, op, rs, rt, name):
        self.out.append(('b', op, rs, rt, name, len(self.out)))

    def beqz(self, rs, name): self._branch(0x04, rs, 'zero', name)
    def bnez(self, rs, name): self._branch(0x05, rs, 'zero', name)
    def bne(self, rs, rt, name): self._branch(0x05, rs, rt, name)

    def assemble(self):
        out = []
        for w in self.out:
            if isinstance(w, tuple):
                _, op, rs, rt, name, idx = w
                here = self.base + 4 * idx
                out.append(_i(op, rs, rt, (self.labels[name] - (here + 4)) >> 2))
            else:
                out.append(w)
        return out


# --------------------------------------------------------------------------
def loader():
    """Call the timer, load the payload once, then call it every frame.

    Three safeguards the first version lacked:
      * ask for the SIZE before reading, instead of requesting 256 KB from a
        44-byte file;
      * only jump into the payload when its first word is the signature, so
        clobbered memory becomes a jump NOT TAKEN rather than a hang;
      * leave a trace (stream pointer, size, bytes read) in a fixed block, so a
        savestate can say what happened.
    """
    a = Asm(CAVE)
    a.raw(0x27BDFFC0)                                   # addiu $sp, -0x40
    a.raw(0xFFBF0030)                                   # sd $ra, 0x30($sp)
    a.raw(0xFFB00028)                                   # sd $s0, 0x28($sp)
    a.raw(0xFFB10020)                                   # sd $s1, 0x20($sp)

    # what the hook replaced
    a.jal(TIMER_UPDATE)
    a.nop()
    a.raw(0xAFA20018)                                   # sw $v0, 0x18($sp)

    a.la('s0', STATE)                                   # s0 = state block
    a.lw('t0', 0, 's0')
    a.bnez('t0', 'call')
    a.nop()

    a.li('t0', 1)                                       # tried (do not repeat)
    a.sw('t0', 0, 's0')

    a.la('a0', NAME)
    a.jal(STREAM_OPEN)
    # $a1 picks the FILE METHOD TABLE: 1 = the coreRaw set (raw open/read/close),
    # which is what the game's own three callers pass. With 0 Stream::Open takes
    # a different structure and returns 0 - that produced state=1 in testing.
    a.li('a1', 1)
    a.sw('v0', 4, 's0')                                 # trace: stream
    a.beqz('v0', 'call')
    a.nop()
    a.move('s1', 'v0')

    a.jal(STREAM_SIZE)                                  # $v0 = size
    a.move('a0', 's1')
    a.sw('v0', 8, 's0')                                 # trace: size
    a.move('a2', 'v0')
    a.la('t0', MAX_SIZE)
    a.sltu('t1', 't0', 'a2')                            # MAX_SIZE < size ?
    a.beqz('t1', 'read')
    a.nop()
    a.move('a2', 't0')                                  # clamp

    a.label('read')
    # $a2 already holds the clamped size from above - do NOT reload MAX_SIZE here
    # or the read asks for 10 KB from a 456-byte file again.
    #
    # The `la` completes BEFORE the `jal`: a two-instruction macro straddling the
    # delay slot would leave its second half executing after the call.
    a.move('a0', 's1')
    a.la('a1', PAYLOAD)
    a.jal(STREAM_READ)
    a.nop()
    a.sw('v0', 12, 's0')                                # trace: bytes read

    a.jal(STREAM_CLOSE)
    a.move('a0', 's1')

    a.li('t0', 2)
    a.sw('t0', 0, 's0')

    # only call when loaded AND the signature matches
    a.label('call')
    a.lw('t0', 0, 's0')
    a.li('t1', 2)
    a.bne('t0', 't1', 'done')
    a.nop()
    a.la('t0', PAYLOAD)
    a.lw('t1', 0, 't0')
    a.la('v1', MAGIC)
    a.bne('t1', 'v1', 'done')
    a.nop()
    a.jal(PAYLOAD + 4)                                  # entry sits after the magic
    a.nop()

    a.label('done')
    a.raw(0x8FA20018)                                   # lw $v0, 0x18($sp)
    a.raw(0xDFBF0030)                                   # ld $ra, 0x30($sp)
    a.raw(0xDFB00028)                                   # ld $s0, 0x28($sp)
    a.raw(0xDFB10020)                                   # ld $s1, 0x20($sp)
    a.jr('ra')
    a.raw(0x27BD0040)                                   # addiu $sp, 0x40
    return a.assemble()


def demo():
    """Proof-of-life payload: writes a signature and counts calls.

    Deliberately dumb. The point is to prove external code RAN, and for that
    something measurable in a savestate is enough.
    """
    a = Asm(PAYLOAD + 4)          # word 0 is the signature
    a.la('t0', PAYLOAD + 0x100)
    a.lui('t1', 0x4D43)                 # 'MC'
    a.ori('t1', 't1', 0x3300)           # 'MC3\0'
    a.sw('t1', 0, 't0')
    a.lw('t1', 4, 't0')
    a.addiu('t1', 't1', 1)              # call counter
    a.sw('t1', 4, 't0')
    a.jr('ra')
    a.nop()
    return a.assemble()


# --------------------------------------------------------------------------
#  pnach output (PCSX2)
# --------------------------------------------------------------------------
def dispatcher():
    """Calls every entry in MODTAB, in order, then returns.

    Deliberately dumb: no state, no error handling, no ordering rules. A module
    that wants to run before another is placed before it in the .ini, which is
    the only ordering an end user can see and reason about.
    """
    a = Asm(DISPATCH)
    a.addiu('sp', 'sp', -0x20)
    a.raw(0xFFBF0000)                     # sd $ra, 0x00($sp)
    a.raw(0xFFB00008)                     # sd $s0, 0x08($sp)
    a.la('s0', MODTAB)

    a.label('loop')
    a.lw('t0', 0, 's0')
    a.beqz('t0', 'fim')
    a.nop()
    a.raw(0x0100F809)                     # jalr $t0
    a.nop()
    a.addiu('s0', 's0', 4)
    a.beqz('zero', 'loop')
    a.nop()

    a.label('fim')
    a.raw(0xDFBF0000)                     # ld $ra, 0x00($sp)
    a.raw(0xDFB00008)                     # ld $s0, 0x08($sp)
    a.addiu('sp', 'sp', 0x20)
    a.jr('ra')
    a.nop()
    return a.assemble()


def early_stub():
    """The boot-time stub: apply what has to exist before the game starts.

    Deliberately does NOT try to load anything. At this point the file system is
    not up, so the external-file route has no payload in RAM yet - and the stub
    says so by simply checking the signature and returning. Only the embedded
    route (and OPL, which writes the payload as codes) is armed this early; the
    normal hooks still handle everything else later.
    """
    a = Asm(EARLY)
    a.addiu('sp', 'sp', -0x20)
    a.raw(0xFFBF0000)                     # sd $ra, 0x00($sp)
    a.raw(0xFFA20008)                     # sd $v0, 0x08($sp)
    a.raw(0xFFA30010)                     # sd $v1, 0x10($sp)
    a.raw(0xFFA40018)                     # sd $a0, 0x18($sp)

    a.la('v0', PAYLOAD)
    a.lw('v1', 0, 'v0')
    a.la('a0', MAGIC)
    a.bne('v1', 'a0', 'out')              # no payload here yet -> do nothing
    a.nop()
    a.jal(PAYLOAD + 4)                    # the entry sits after the signature
    a.nop()

    a.label('out')
    a.raw(0xDFBF0000)                     # ld $ra, 0x00($sp)
    a.raw(0xDFA20008)                     # ld $v0, 0x08($sp)
    a.raw(0xDFA30010)                     # ld $v1, 0x10($sp)
    a.raw(0xDFA40018)                     # ld $a0, 0x18($sp)
    a.addiu('sp', 'sp', 0x20)
    a.raw(EARLY_WORD)                     # the `ei` the hook replaced
    a.jr('ra')
    a.nop()
    return a.assemble()


def _pnach_words(base, words, mode=2):
    return ['patch=%d,EE,%08X,word,%08X' % (mode, base + 4 * i, w)
            for i, w in enumerate(words)]


def write_pnach(out_path, embed=None, path=PATH, early=False):
    # the string block runs from NAME to STATE; overflowing would clobber state
    if len(path) + 1 > STATE - NAME:
        raise SystemExit('path of %d bytes does not fit the %d reserved'
                         % (len(path) + 1, STATE - NAME))
    L = loader()
    out = []
    out.append('gametitle=Midnight Club 3: DUB Edition Remix (SLUS-21355)')
    out.append('comment=%s' % ('Payload embedded in this file (works from ISO)'
                               if embed else 'External payload loader (HostFS)'))
    out.append('')
    out.append('// ' + '-' * 72)
    out.append('//  This file carries only the LOADER (%d instructions) at %08X.'
               % (len(L), CAVE))
    if embed:
        out.append('//  The payload is EMBEDDED here, so it needs no loose file and no')
        out.append('//  HostFS: it works when running from an ISO. The trade is that')
        out.append('//  rebuilding the payload means regenerating this file.')
    else:
        out.append('//  Everything else lives in "%s", read at run time' % path)
        out.append("//  through the game's own Stream API and called once per frame.")
        out.append('//  Rebuilding the payload does NOT touch this file.')
    out.append('//')
    out.append('//  Map:  loader %08X   path %08X   state %08X   payload %08X'
               % (CAVE, NAME, STATE, PAYLOAD))
    out.append('//  The cave sits INSIDE the ELF image (12 KB of zeros at 0x61C858,')
    out.append('//  confirmed untouched in a gameplay savestate). RAM outside the image')
    out.append('//  does NOT work: the boot zeroes it after the patch writes.')
    out.append('//')
    out.append('//  Hooks: the four `jal datTimeManager::Update` in mcGame. The three')
    out.append('//  menu ones make the load happen with the file system idle; the one in')
    out.append('//  mcGame::Update only runs once the city is up. The loader calls the')
    out.append('//  real timer before anything else, so the game is unchanged.')
    out.append('// ' + '-' * 72)
    out.append('')
    out.append('[Modloader\\Embedded payload]' if embed
               else '[Modloader\\External payload loader]')
    if embed:
        out.append('description=Payload embedded in this file, executed once per frame. '
                   'Needs no loose file and no HostFS - works when running from an ISO.')
    else:
        out.append('description=Reads "%s" and executes it once per frame. '
                   'Without the file the loader marks failure and the game runs '
                   'normally.' % path)

    b = path.encode('ascii') + b'\0'
    b += b'\0' * ((-len(b)) % 4)
    name_w = list(struct.unpack('<%dI' % (len(b) // 4), b))

    out.append('')
    out.append('// the loader')
    out += _pnach_words(CAVE, L)

    if embed is None:
        out.append('// the file path')
        out += _pnach_words(NAME, name_w)
        out.append('// state: patch=0, ON STARTUP ONLY. With patch=1/2 the file would')
        out.append('// zero the flag every frame and the payload would reload forever.')
        out += _pnach_words(STATE, [0], mode=0)
        total = len(L) + len(name_w) + 1
    else:
        # Embedded: the payload rides along, so there is no file to open and the
        # loader never calls the Stream API. State starts at 2 ("loaded") and the
        # loader drops straight into the signature check and the call.
        pw = list(struct.unpack('<%dI' % (len(embed) // 4), embed))
        out.append('// the payload, %d words (%d bytes) - no external file'
                   % (len(pw), len(embed)))
        out += _pnach_words(PAYLOAD, pw)
        out.append('// state already 2 = loaded: there is nothing to open')
        out += _pnach_words(STATE, [2], mode=0)
        total = len(L) + len(pw) + 1

    if early:
        out.append('')
        out.append('// the boot stub, and the `ei` at the end of crt0 that calls it.')
        out.append('// Runs before ps2main, so the patch groups are in place before the')
        out.append('// game reads them. Does nothing unless the payload is already in')
        out.append('// RAM, which for the external-file route it is not - that one is')
        out.append('// still handled by the hooks below.')
        out += _pnach_words(EARLY, early_stub())
        out += _pnach_words(EARLY_HOOK, [0x0C000000 | ((EARLY >> 2) & 0x3FFFFFF)])

    out.append('// the hooks: jal datTimeManager::Update -> jal loader')
    for g in HOOKS:
        out.append('patch=2,EE,%08X,word,%08X'
                   % (g, 0x0C000000 | ((CAVE >> 2) & 0x3FFFFFF)))
    io.open(out_path, 'w', encoding='latin1').write('\n'.join(out) + '\n')
    if early:
        total += len(early_stub()) + 1
    print('  %-32s loader %d, %s, %d words%s'
          % (os.path.basename(out_path), len(L),
             ('payload EMBEDDED %d bytes' % len(embed)) if embed
             else 'payload in an external file', total + len(HOOKS),
             ', boot stub' if early else ''))
    return out_path


# --------------------------------------------------------------------------
#  .cht output (OPL, real PS2)
# --------------------------------------------------------------------------
#  OPL's cheat engine applies raw 32-bit codes as `2AAAAAAA VVVVVVVV`, where the
#  leading 2 is the type (word write) and the rest is the EE address. Every code
#  is reapplied EVERY FRAME - there is no equivalent of PCSX2's patch=0, which
#  writes only at startup.
#
#  That is why .cht only makes sense with an embedded payload: there everything
#  is idempotent (loader, payload and state=2 are always the same values). In
#  external mode the state starts at 0, would be rewritten to 0 every frame, and
#  the payload would reload from the file forever.
def write_cht(out_path, embed, path=PATH_ISO, early=False,
              title='SLUS-21355 Midnight Club 3 DUB Edition Remix'):
    L = loader()

    def block(base, words):
        return ['2%07X %08X' % ((base + 4 * i) & 0xFFFFFFF, w)
                for i, w in enumerate(words)]

    if embed is not None:
        pw = list(struct.unpack('<%dI' % (len(embed) // 4), embed))
        out = ['"%s"' % title, 'Modloader - embedded payload']
        out += block(CAVE, L)
        out += block(PAYLOAD, pw)
        out += block(STATE, [2])      # 2 = loaded; reapplying it is harmless
        n = len(L) + len(pw) + 1
        como = 'loader %d, payload %d, state 1' % (len(L), len(pw))
    else:
        # External file, and the state code is simply NOT emitted. That was the
        # whole objection to this mode: OPL would rewrite state=0 every frame and
        # the payload would reload forever. It never had to be written at all -
        # boot zeroes EE RAM, so the flag already starts at 0 and the loader is
        # the only thing that ever sets it to 2. Nothing to reapply, nothing to
        # fight. Costs ~650 codes less than embedding, which is the difference
        # between fitting in OPL and not.
        b = path.encode('ascii') + b'\0'
        b += b'\0' * ((-len(b)) % 4)
        name_w = list(struct.unpack('<%dI' % (len(b) // 4), b))
        out = ['"%s"' % title, 'Modloader - payload from %s' % path]
        out += block(CAVE, L)
        out += block(NAME, name_w)
        n = len(L) + len(name_w)
        como = 'loader %d, path %d, no state code' % (len(L), len(name_w))

    if early:
        es = early_stub()
        out += block(EARLY, es)
        out += block(EARLY_HOOK, [0x0C000000 | ((EARLY >> 2) & 0x3FFFFFF)])
        n += len(es) + 1
        como += ', boot stub %d' % len(es)

    out += ['2%07X %08X' % (g & 0xFFFFFFF,
                            0x0C000000 | ((CAVE >> 2) & 0x3FFFFFF))
            for g in HOOKS]

    io.open(out_path, 'w', encoding='latin1').write('\n'.join(out) + '\n')
    n += len(HOOKS)
    print('  %-32s %d codes (%s, hooks %d)'
          % (os.path.basename(out_path), n, como, len(HOOKS)))


# --------------------------------------------------------------------------
#  savestate inspection
# --------------------------------------------------------------------------
def ee_from_savestate(p):
    """EE RAM out of a .p2s. PCSX2 2.x compresses savestates with zstd inside a
    ZIP, which neither `zipfile` nor older 7-Zip can open: skip the local header
    by hand and decompress the member."""
    import zipfile
    import zstandard
    z = zipfile.ZipFile(p)
    i = [x for x in z.infolist() if x.filename == 'eeMemory.bin'][0]
    z.fp.seek(i.header_offset)
    n, m = struct.unpack_from('<HH', z.fp.read(30), 26)
    z.fp.seek(i.header_offset + 30 + n + m)
    return zstandard.ZstdDecompressor().decompress(
        z.fp.read(i.compress_size), max_output_size=i.file_size)


STATES = {0: 'not tried', 1: 'FAILED', 2: 'loaded'}


def report_state(p):
    """Read the trace block from a savestate and say where the loader stopped."""
    d = ee_from_savestate(p)
    dw = lambda va: struct.unpack_from('<I', d, va & 0x1FFFFFF)[0]
    f32 = lambda va: struct.unpack_from('<f', d, va & 0x1FFFFFF)[0]
    st = dw(STATE)
    raw = d[NAME & 0x1FFFFFF:(NAME & 0x1FFFFFF) + 24]
    print('%s' % os.path.basename(p))
    print('   hook %08X      = %08X %s'
          % (HOOKS[-1], dw(HOOKS[-1]),
             'ok' if dw(HOOKS[-1]) == (0x0C000000 | (CAVE >> 2)) else 'NOT APPLIED'))
    print('   loader installed  = %s'
          % ('yes' if dw(CAVE) == 0x27BDFFC0 else 'NO (%08X)' % dw(CAVE)))
    print('   path              = %r' % raw.split(bytes(1))[0])
    print('   state             = %d (%s)' % (st, STATES.get(st, '?')))
    print('   stream            = %08X' % dw(STATE + 4))
    print('   size / read       = %d / %d' % (dw(STATE + 8), dw(STATE + 12)))
    print('   payload signature = %08X %s'
          % (dw(PAYLOAD), 'ok' if dw(PAYLOAD) == MAGIC else 'WRONG'))
    print('   game frames       = %d' % dw(0x618E3C))

    # The patch table only exists in payloads built with patches.cpp, so an old
    # payload simply reports nothing here instead of printing garbage.
    if dw(TELEM) == TELEM_MAGIC:
        dt = struct.unpack('<f', struct.pack('<I', dw(TELEM + 24)))[0]
        print('   --- patch groups (payload) ---')
        print('   groups enabled    = %d' % dw(TELEM + 4))
        print('   writes per pass   = %d' % dw(TELEM + 8))
        print('   writes changed    = %d (cumulative)' % dw(TELEM + 12))
        print('   cache flushes     = %d' % dw(TELEM + 16))
        print('   passes            = %d' % dw(TELEM + 20))
        print('   dt last pass      = %.7g%s'
              % (dt, ('  -> %.2f Hz' % (1.0 / dt)) if dt > 0 else ''))
        # After the first pass everything already holds the patched value, so
        # `changed` must stop growing. If it keeps climbing, something in the
        # game is writing those words back every frame and the patch is losing
        # a race it looks like it won.
        if dw(TELEM + 20) > 4 and dw(TELEM + 12) > dw(TELEM + 8):
            print('   -> `changed` above one pass: the game is rewriting these '
                  'words, patch is fighting it')
    elif dw(PAYLOAD) == MAGIC:
        print('   patch groups      = none (payload built without patches.cpp)')

    if dw(CORE_TRACE) == 0x4D433352:
        ERR = {0: 'ok', 1: 'Stream::Open falhou', 2: 'tamanho invalido',
               3: 'alocacao falhou', 4: 'leitura curta', 5: 'formato invalido'}
        print('   --- deferred modules (core.mod) ---')
        print('   core rodou       = %d vez(es)' % dw(CORE_TRACE + 4))
        print('   adiados na lista = %d' % dw(CORE_TRACE + 8))
        print('   carregados       = %d' % dw(CORE_TRACE + 12))
        print('   hooks instalados = %d' % dw(CORE_TRACE + 16))
        print('   bytes do heap    = %d' % dw(CORE_TRACE + 28))
        e = dw(CORE_TRACE + 20)
        if e:
            print('   ultima falha     = %s' % ERR.get(e, '? %d' % e))

    if dw(PUBLIC_TELEM) == PUBLIC_TELEM_MAGIC:
        version_size = dw(PUBLIC_TELEM + 4)
        version = version_size & 0xFFFF
        size = version_size >> 16
        sequence = dw(PUBLIC_TELEM + 8)
        flags = dw(PUBLIC_TELEM + 12)
        gear = dw(PUBLIC_TELEM + 24)
        if gear & 0x80000000:
            gear -= 0x100000000
        gear_name = 'R' if gear == -1 else ('N' if gear == 0 else str(gear))
        print('   --- public telemetry (mc3_telemetry.mod) ---')
        print('   ABI               = v%d, %d bytes' % (version, size))
        print('   sequence/flags    = %d%s / %08X' % (
            sequence, ' (WRITING)' if sequence & 1 else '', flags))
        print('   speed / rpm / gear= %.2f km/h / %.1f rpm / %s' % (
            f32(PUBLIC_TELEM + 16), f32(PUBLIC_TELEM + 20), gear_name))
        print('   velocity xyz      = %.4f / %.4f / %.4f' % (
            f32(PUBLIC_TELEM + 28), f32(PUBLIC_TELEM + 32),
            f32(PUBLIC_TELEM + 36)))
        print('   player address    = %08X' % dw(PUBLIC_TELEM + 40))

    if dw(TRACE) == TRACE_MAGIC:
        idx = dw(TRACE + 4)
        raw = d[(TRACE + 24) & 0x1FFFFFF:(TRACE + 24 + 72) & 0x1FFFFFF]
        print('   --- modules (chain loader) ---')
        print('   ini opened        = %s'
              % ('none' if idx == 0xFFFFFFFF else 'INI_PATHS[%d]' % idx))
        print('   modules tried     = %d' % dw(TRACE + 8))
        print('   modules loaded    = %d' % dw(TRACE + 12))
        err = dw(TRACE + 16)
        if err:
            print('   last failure      = %s' % TRACE_ERR.get(err, '? %d' % err))
            print('   failing path      = %r' % raw.split(bytes(1))[0])
            print('   bytes read        = %d' % dw(TRACE + 20))

    # undefined_syscall_trace.mod is relocatable, so its report has no fixed
    # VA. A freed Stream buffer can retain a second byte-for-byte copy of the
    # .mod file, including USCT; prefer the initialized live report instead of
    # assuming the first occurrence is unique.
    usct_candidates = []
    search_at = 0
    while True:
        found = d.find(b'USCT', search_at)
        if found < 0:
            break
        search_at = found + 4
        if found + 44 <= len(d) and dw(found) == 0x54435355:
            installed = dw(found + 4)
            replaced = dw(found + 8)
            hits = dw(found + 12)
            if installed <= 1 and replaced <= 128:
                usct_candidates.append(found)
    usct = max(usct_candidates,
               key=lambda p: (dw(p + 4) == 1, dw(p + 12) > 0,
                              dw(p + 8) > 0, p)) if usct_candidates else -1
    if usct >= 0:
        print('   --- undefined syscall trace (undefined_syscall_trace.mod) ---')
        print('   report address    = %08X' % usct)
        if len(usct_candidates) > 1:
            print('   USCT candidates   = %d (live report selected)' %
                  len(usct_candidates))
        print('   installed/replaced= %d / %d entries' %
              (dw(usct + 4), dw(usct + 8)))
        hits = dw(usct + 12)
        print('   hits              = %d' % hits)
        if hits:
            print('   raw v1 / syscall  = %08X / %d' %
                  (dw(usct + 16), dw(usct + 20)))
            print('   EPC / instruction = %08X / %08X' %
                  (dw(usct + 24), dw(usct + 40)))
            print('   Cause / BIOS RA/SP= %08X / %08X / %08X' %
                  (dw(usct + 28), dw(usct + 32), dw(usct + 36)))
            if usct + 164 <= len(d) and dw(usct + 44) == 2:
                print('   tracer version    = 2')
                print('   game RA / SP      = %08X / %08X' %
                      (dw(usct + 48), dw(usct + 52)))
                print('   v0 / a0-a3        = %08X / %08X %08X %08X %08X' %
                      (dw(usct + 56), dw(usct + 60), dw(usct + 64),
                       dw(usct + 68), dw(usct + 72)))
                print('   s0-s3             = %08X %08X %08X %08X' %
                      (dw(usct + 116), dw(usct + 120), dw(usct + 124),
                       dw(usct + 128)))
                print('   s4-s7             = %08X %08X %08X %08X' %
                      (dw(usct + 132), dw(usct + 136), dw(usct + 140),
                       dw(usct + 144)))
        else:
            print('   -> tracer loaded, but no undefined syscall reached it')

    # prop_virtual_guard_test.mod is also relocatable and its source .mod may
    # remain in a freed Stream buffer. Select the initialized report exactly as
    # above instead of trusting the first PVGD byte sequence in RAM.
    pvgd_candidates = []
    search_at = 0
    while True:
        found = d.find(b'PVGD', search_at)
        if found < 0:
            break
        search_at = found + 4
        if found + 56 <= len(d) and dw(found) == 0x44475650:
            if dw(found + 4) in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16) and dw(found + 8) <= 1 and dw(found + 12) <= 1:
                pvgd_candidates.append(found)
    pvgd = max(pvgd_candidates,
               key=lambda p: (dw(p + 8) == 1, dw(p + 12) == 1,
                              dw(p + 20) > 0,
                              dw(p + 148) > 0 if dw(p + 4) >= 6 and
                              p + 152 <= len(d) else False,
                              p)) if pvgd_candidates else -1
    if pvgd >= 0:
        print('   --- prop virtual guard (prop_virtual_guard_test.mod) ---')
        print('   report address    = %08X' % pvgd)
        if len(pvgd_candidates) > 1:
            print('   PVGD candidates   = %d (live report selected)' %
                  len(pvgd_candidates))
        version = dw(pvgd + 4)
        print('   version           = %d' % version)
        print('   installed/patched = %d / %d' %
              (dw(pvgd + 8), dw(pvgd + 12)))
        print('   original word     = %08X' % dw(pvgd + 16))
        hits = dw(pvgd + 20)
        print('   invalid calls     = %d' % hits)
        if hits:
            print('   object / index    = %08X / %08X' %
                  (dw(pvgd + 24), dw(pvgd + 28)))
            print('   vtable / method   = %08X / %08X' %
                  (dw(pvgd + 32), dw(pvgd + 36)))
            print('   manager / array   = %08X / %08X' %
                  (dw(pvgd + 40), dw(pvgd + 44)))
            print('   slot now / RA     = %08X / %08X' %
                  (dw(pvgd + 48), dw(pvgd + 52)))
        if version >= 2 and pvgd + 64 <= len(d):
            print('   refreshed/skipped = %d / %d' %
                  (dw(pvgd + 56), dw(pvgd + 60)))
        if version >= 3 and pvgd + 92 <= len(d):
            print('   packet patched    = %d (original %08X)' %
                  (dw(pvgd + 88), dw(pvgd + 84)))
            packet_hits = dw(pvgd + 64)
            print('   bad packet lists  = %d' % packet_hits)
            if packet_hits:
                print('   packet obj/view   = %08X / %08X' %
                      (dw(pvgd + 68), dw(pvgd + 72)))
                print('   count / caller RA = %08X / %08X' %
                      (dw(pvgd + 76), dw(pvgd + 80)))
                if version >= 4 and pvgd + 100 <= len(d):
                    print('   list off / limit  = %08X / %08X' %
                          (dw(pvgd + 92), dw(pvgd + 96)))
        if version >= 5 and pvgd + 148 <= len(d):
            print('   shader patched    = %d (orig %08X / %08X)' %
                  (dw(pvgd + 144), dw(pvgd + 136), dw(pvgd + 140)))
            shader_hits = dw(pvgd + 100)
            print('   bad shader calls  = %d' % shader_hits)
            if shader_hits:
                print('   shader obj/vtable = %08X / %08X' %
                      (dw(pvgd + 104), dw(pvgd + 108)))
                print('   method/model      = %08X / %08X' %
                      (dw(pvgd + 112), dw(pvgd + 116)))
                print('   group/geom        = %08X / %08X' %
                      (dw(pvgd + 120), dw(pvgd + 124)))
                print('   caller RA / site  = %08X / %d' %
                      (dw(pvgd + 128), dw(pvgd + 132)))
        if version >= 6 and pvgd + 180 <= len(d):
            print('   state patched     = %d (orig %08X / %08X)' %
                  (dw(pvgd + 176), dw(pvgd + 168), dw(pvgd + 172)))
            state_hits = dw(pvgd + 148)
            print('   bad render states = %d' % state_hits)
            if state_hits:
                state_reason = dw(pvgd + 156)
                reason_name = {
                    1: 'null',
                    2: 'unaligned',
                    3: 'outside EE RAM',
                    4: 'bad DMA header',
                    5: 'bad VIF DIRECT6 header',
                    6: 'GIFtag has no EOP',
                }.get(state_reason, 'unknown')
                print('   state obj/reason  = %08X / %d (%s)' %
                      (dw(pvgd + 152), state_reason, reason_name))
                print('   caller RA/skipped = %08X / %d' %
                      (dw(pvgd + 160), dw(pvgd + 164)))
        if version >= 7 and pvgd + 248 <= len(d) and shader_hits:
            draw_ra = dw(pvgd + 180)
            parent_ra = dw(pvgd + 184)
            writer_ra = parent_ra if draw_ra == 0x002A9AD8 and parent_ra else draw_ra
            writer_names = {
                0x0024C4C8: 'direct Draw: group table + model selector',
                0x00257150: 'direct Draw: group table + model selector',
                0x0025C6C8: 'direct Draw: global model manager +0x10',
                0x002A80C8: 'direct Draw: a1 forwarded by generic draw path',
                0x003B1F20: 'prop draw: a1 built from saved s2 owner +4/+8',
                0x00563FB4: 'direct Draw: group table + object selector',
            }
            print('   Draw caller/parent= %08X / %08X' % (draw_ra, parent_ra))
            print('   s5 writer         = %08X (%s)' %
                  (writer_ra, writer_names.get(writer_ra, 'unmapped call site')))
            print('   caller saved s0-s3= %08X %08X %08X %08X' %
                  (dw(pvgd + 188), dw(pvgd + 192), dw(pvgd + 196),
                   dw(pvgd + 200)))
            print('   caller saved s4-s6= %08X %08X %08X' %
                  (dw(pvgd + 204), dw(pvgd + 208), dw(pvgd + 212)))
            source_kind = dw(pvgd + 216)
            if source_kind == 1:
                source_owner = dw(pvgd + 220)
                source_selector = dw(pvgd + 224)
                source_base = dw(pvgd + 228)
                source_expected = dw(pvgd + 232)
                print('   prop owner/select = %08X / %02X' %
                      (source_owner, source_selector))
                print('   source base/result= %08X / %08X' %
                      (source_base, source_expected))
                if source_expected == dw(pvgd + 120):
                    print('   -> confirmed: this prop owner produced the live s5')
                else:
                    print('   -> WARNING: reconstructed prop source differs from live s5')
            elif source_kind == 2:
                print('   source route      = rmcModel::Draw wrapper; parent RA is the writer')
            print('   group vtbl/slots  = %08X / %08X' %
                  (dw(pvgd + 236), dw(pvgd + 240)))
            print('   group slot[4]     = %08X' % dw(pvgd + 244))
        if version >= 8 and pvgd + 312 <= len(d):
            pagein_hits = dw(pvgd + 252)
            print('   legacy ctor calls = %d (target 0x5C: %d)' %
                  (dw(pvgd + 248), pagein_hits))
            if pagein_hits:
                print('   PageIn group/load = %08X / %08X' %
                      (dw(pvgd + 256), dw(pvgd + 260)))
                print('   before vt/slots   = %08X / %08X' %
                      (dw(pvgd + 264), dw(pvgd + 268)))
                print('   before count/list = %08X / %08X' %
                      (dw(pvgd + 272), dw(pvgd + 276)))
                print('   before slot0/4    = %08X / %08X' %
                      (dw(pvgd + 280), dw(pvgd + 284)))
                print('   after vt/slots    = %08X / %08X' %
                      (dw(pvgd + 288), dw(pvgd + 292)))
                print('   after count/list  = %08X / %08X' %
                      (dw(pvgd + 296), dw(pvgd + 300)))
                print('   after slot0/4     = %08X / %08X' %
                      (dw(pvgd + 304), dw(pvgd + 308)))
        if version >= 10 and pvgd + 376 <= len(d):
            constructor_hits = dw(pvgd + 316)
            target_label = ('manager group site: %d' if version >= 14 else
                            'target 0x5C: %d')
            print(('   all ctor calls    = %d (' + target_label + ')') %
                  (dw(pvgd + 312), constructor_hits))
            if constructor_hits:
                print('   ctor site/group   = %08X / %08X' %
                      (dw(pvgd + 320), dw(pvgd + 324)))
                print('   loader / delta    = %08X / %08X' %
                      (dw(pvgd + 328), dw(pvgd + 332)))
                print('   slots before/after= %08X / %08X' %
                      (dw(pvgd + 336), dw(pvgd + 340)))
                print('   count before/after= %08X / %08X' %
                      (dw(pvgd + 344), dw(pvgd + 348)))
                print('   list before/after = %08X / %08X' %
                      (dw(pvgd + 352), dw(pvgd + 356)))
                print('   slot0/4 before    = %08X / %08X' %
                      (dw(pvgd + 360), dw(pvgd + 364)))
                print('   slot0/4 after     = %08X / %08X' %
                      (dw(pvgd + 368), dw(pvgd + 372)))
        if version >= 11 and pvgd + 432 <= len(d):
            context_hits = dw(pvgd + 380)
            print('   context fixups    = %d (Atlanta target: %d)' %
                  (dw(pvgd + 376), context_hits))
            if context_hits:
                print('   context site/addr = %08X / %08X' %
                      (dw(pvgd + 384), dw(pvgd + 388)))
                print('   context load/delta= %08X / %08X' %
                      (dw(pvgd + 392), dw(pvgd + 396)))
                print('   b8 raw/predicted  = %08X / %08X' %
                      (dw(pvgd + 400), dw(pvgd + 404)))
                print('   b8 after / manager= %08X / %08X' %
                      (dw(pvgd + 408), dw(pvgd + 412)))
                phase = ('post-read/pre-relocation' if version >= 13 else
                         ('pre-fixup' if version >= 12 else 'after'))
                print('   group %s vt/slots= %08X / %08X' %
                      (phase, dw(pvgd + 416), dw(pvgd + 420)))
                print('   group %s count/list= %08X / %08X' %
                      (phase, dw(pvgd + 424), dw(pvgd + 428)))
        if version >= 12 and pvgd + 452 <= len(d):
            if version >= 16:
                print('   group ctor detour = %d (orig %08X / %08X)' %
                      (dw(pvgd + 448), dw(pvgd + 440), dw(pvgd + 444)))
            elif version >= 14:
                print('   mcPropType detours= retired (entry orig %08X / %08X)' %
                      (dw(pvgd + 440), dw(pvgd + 444)))
            else:
                print('   entry detour      = %d (orig %08X / %08X)' %
                      (dw(pvgd + 448), dw(pvgd + 440), dw(pvgd + 444)))
            if version >= 14:
                pass
            elif version >= 13:
                print('   entry calls       = %d (pre-read; no target filter)' %
                      dw(pvgd + 432))
            else:
                print('   entry calls       = %d (Atlanta target: %d)' %
                      (dw(pvgd + 432), dw(pvgd + 436)))
        if version >= 13 and pvgd + 472 <= len(d):
            if version < 14:
                print('   post-read detour  = %d (orig %08X / %08X)' %
                      (dw(pvgd + 468), dw(pvgd + 460), dw(pvgd + 464)))
                print('   post-read calls   = %d (Atlanta target: %d)' %
                      (dw(pvgd + 452), dw(pvgd + 456)))

    if dw(MODREPORT) == 0x4D433342:          # 'MC3B', mc2_probe.mod
        _probe_report(d, dw)
    elif dw(MODREPORT) == 0x4D433359:        # 'MC3Y', city_slot6.mod
        _city_report(d, dw)
    elif dw(MODREPORT) == 0x4D433348:        # 'MC3H', mesh_dump.mod
        _dump_report(d, dw)
    elif dw(MODREPORT) == 0x4D433354:        # 'MC3T', pck_tool.mod
        _tool_report(d, dw)

    # Its own corner of the same window, past what city_slot6 uses, so both
    # modules can report on the same boot.
    if dw(MODREPORT + 200) == 0x4D43334E:    # 'MC3N', menu_row.mod
        M = MODREPORT + 200
        print('   --- top-level menu row (menu_row.mod) ---')
        print('   vtable slot       = %s (held %08X)'
              % ('redirected' if dw(M + 4) else 'NOT TOUCHED - unexpected value',
                 dw(M + 20)))
        print('   PopulateMenu runs = %d' % dw(M + 8))
        print('   row added         = %d   list %08X' % (dw(M + 12), dw(M + 16)))
        print('   row pressed       = %d' % dw(M + 24))
        print('   SetCity redirected= %d time(s) to city 5' % dw(M + 28))
        print('   second sink fixed = %d time(s)   (it held %d)'
              % (dw(M + 32), dw(M + 36)))
        if dw(M + 8) and not dw(M + 12):
            print('   -> the hook ran but never reached AddItemSimple: the screen '
                  'or list pointer did not look valid')
        elif dw(M + 24) and not dw(M + 28):
            print('   -> pressed, but screen 28 never asked for a city on the '
                  'route we intercept')
        elif dw(M + 28):
            print('   -> city 5 was requested on both sinks.')

    if dw(WS_REPORT) == 0x4D433357:           # 'MC3W', proper widescreen
        requested = dw(WS_REPORT + 4)
        current = dw(WS_REPORT + 8)
        names = {0: '4:3', 1: '16:9', 2: '21:9', 0xFFFFFFFF: 'pending'}
        setres_words = (dw(0x00527E14), dw(0x00527E18))
        setres_profiles = {
            (0x3C013FAA, 0x3421AAAA): 0,
            (0x3C013FE3, 0x34218E34): 1,
            (0x3C014018, 0x34218E34): 2,
        }
        setres_profile = setres_profiles.get(setres_words)
        display_aspects = {0: 1.3333333, 1: 1.7777778, 2: 2.3836794}
        print('   --- proper widescreen ---')
        print('   requested profile = %s' % names.get(requested, '? %d' % requested))
        print('   active profile    = %s' % names.get(current, '? %d' % current))
        print('   profile switches  = %d' % dw(WS_REPORT + 12))
        print('   SetRes seed code  = %s (%08X / %08X)' % (
            names.get(setres_profile, 'unknown'), setres_words[0], setres_words[1]))

        # 00527E14/18 only seed the next gfxPipeline::SetRes calculation.  The
        # live camera is rebuilt by gfxViewport::Perspective and keeps its real
        # aspect/matrix in the active viewport.  Report both so patched code is
        # never again mistaken for proof that the rendered 3D view changed.
        pipe_width = f32(0x0070E5F4)
        pipe_height = f32(0x0070E5F8)
        pipe_base = f32(0x0061C318)
        viewport = dw(0x0070E50C)
        print('   projection base   = %.7g  pipeline %.7g x %.7g' % (
            pipe_base, pipe_width, pipe_height))
        if 0x00100000 <= viewport < 0x02000000 and not (viewport & 3):
            view_width = dw(viewport + 0x174)
            view_height = dw(viewport + 0x178)
            cached_aspect = f32(viewport + 0x134)
            passed_aspect = f32(viewport + 0x1A0)
            matrix_x = f32(viewport + 0x10)
            matrix_y = f32(viewport + 0x24)
            matrix_aspect = matrix_y / matrix_x if abs(matrix_x) > 1.0e-12 else 0.0
            print('   active viewport   = %08X  %d x %d' % (
                viewport, view_width, view_height))
            print('   live aspect       = cached %.7g / passed %.7g / matrix %.7g' % (
                cached_aspect, passed_aspect, matrix_aspect))

            if (current in display_aspects and pipe_width > 0.0 and
                    pipe_height > 0.0 and view_width > 0 and view_height > 0):
                expected = ((pipe_height * display_aspects[current] / pipe_width) *
                            float(view_width) / float(view_height))
                print('   expected viewport = %.7g' % expected)
                if passed_aspect != 0.0 and abs(matrix_aspect - expected) > 0.02:
                    print('   -> WARNING: live projection matrix does not match the '
                          'selected profile')
        else:
            print('   active viewport   = unavailable (%08X)' % viewport)
        if current != requested:
            print('   -> selection is waiting for proper_widescreen.mod; verify it '
                  'is loaded as `defer` and core.mod is active')
        elif setres_profile != current:
            print('   -> WARNING: another module or PNACH is overwriting the selected '
                  'SetRes seed; disable the fixed widescreen patch')

    if dw(MODREPORT + 176) == 0x4D433347:     # 'MC3G', city_place.mod
        P = MODREPORT + 176
        # Written before each step, cleared on success: a value here after a
        # hang names the call that never returned.
        ERR = {0: 'ok', 1: 'Stream::Open', 2: 'Stream::Size',
               3: 'alocacao da tabela (610 KB)', 4: 'Stream::Read',
               5: 'magic errado', 6: 'alocacao dos slots'}
        print('   --- MC2 placement (city_place.mod) ---')
        print('   table            = %08X   %d entries'
              % (dw(P + 4), dw(P + 8)))
        print('   models resolved  = %d' % dw(P + 12))
        print('   draws last frame = %d' % dw(P + 16))
        e = dw(P + 20)
        if e and not dw(P + 4):
            print('   stopped at       = %s' % ERR.get(e, '? %d' % e))
        elif e:
            print('   last failure     = %s' % ERR.get(e, '? %d' % e))
        elif not dw(P + 4):
            print('   -> waiting: it only runs once the session city index is 5, '
                  'so this is normal anywhere but inside the new city')
        elif dw(P + 12) and not dw(P + 16):
            print('   -> models loaded but nothing drawn: the hook runs, so this '
                  'is the model pointers, not the table')

    _log_rings(d)

    if st == 1 and dw(STATE + 4) == 0:
        print('   -> Stream::Open failed: wrong path, or the file is missing')
    elif st == 2 and dw(STATE + 12) < dw(STATE + 8):
        print('   -> short read: Stream::Read stopped early')


# --------------------------------------------------------------------------
LOG_MAGIC_V2 = 0x4D43334B        # 'MC3K'


def _log_rings(d):
    """Every ring in RAM, found by scanning for the head magic.

    v1 kept its head at a fixed address and that collided with another fork's
    report - twice over, in a window several forks now write to. v2 puts the
    head in each module's own relocated .data instead, so there is no address
    to agree on and any number of modules can log at once. The cost is this
    scan, which is a memchr over 32 MB and takes no noticeable time.
    """
    alvo = struct.pack('<I', LOG_MAGIC_V2)
    achados, i = [], 0
    while True:
        i = d.find(alvo, i)
        if i < 0 or i > 0x2000000 - 32:
            break
        ver, dono, anel, tam, cur, seq, perd = struct.unpack_from('<7I', d, i + 4)
        # The magic alone is a word anyone could hold. A head is only a head if
        # the rest of it is sane too.
        if (ver == 2 and 0 < tam <= 0x100000 and anel
                and 0x00100000 <= anel < 0x02000000 and cur <= 0xFFFFFFF):
            achados.append((i, dono, anel, tam, cur, seq, perd))
        i += 4
    if not achados:
        return

    for _off, dono, anel, tam, cur, seq, perd in achados:
        tag = ''.join(chr((dono >> s) & 0xFF) for s in (24, 16, 8, 0))
        print('   --- log %r ---  %d registro(s), %d bytes, anel %08X%s'
              % (tag, seq, cur, anel, ('  %d PERDIDOS' % perd) if perd else ''))
        _dump_ring(d, anel, tam, cur)


def _dump_ring(d, anel, size, cursor):
    base = anel & 0x1FFFFFF
    # A ring that never wrapped starts at 0; one that did starts at the cursor.
    inicio = 0 if cursor < size else cursor % size
    pos, lidos, deu_volta = inicio, 0, False
    while lidos < 4000:
        total = d[base + pos] | (d[base + pos + 1] << 8)
        if total < 5 or pos + total > size:
            if deu_volta or inicio == 0:
                break
            pos, deu_volta = 0, True
            continue
        nv, dec = d[base + pos + 2], d[base + pos + 3]
        fim = d.find(bytes(1), base + pos + 4, base + pos + total)
        if fim < 0:
            break
        txt = d[base + pos + 4:fim].decode('latin1', 'replace')
        vals = [struct.unpack_from('<I', d, fim + 1 + k * 4)[0] for k in range(nv)]
        corpo = (' '.join(str(v) for v in vals) if dec
                 else ' '.join('%08X' % v for v in vals))
        print('      %-40s %s' % (txt, corpo))
        pos = (pos + total) % size
        lidos += 1
        if pos == inicio:
            break


# --------------------------------------------------------------------------


def _tool_report(d, dw):
    """pck_tool.mod: the frontend replaced by the conversion tool.

    Same window as mesh_dump, one word further on: the tool tracks which model
    it is inside (`running`) so a hang names the file that caused it, and it
    reports the heap range mc3_pckbuild.py cuts the .pck out of.
    """
    ESTADO = {0: 'aquecendo', 1: 'pronto', 2: 'convertendo',
              3: 'PRONTO - salve um savestate', 4: 'SEM HEAP'}
    b = MODREPORT
    estado = dw(b + 8)
    print('pck_tool.mod  %s' % ESTADO.get(estado, 'estado %d' % estado))
    print('  frames             %d' % dw(b + 4))
    print('  livre antes        %d bytes' % dw(b + 12))
    desenhos = dw(b + 16)
    print('  quadros desenhados %d   %s'
          % (desenhos, 'redirect de vtable ok' if desenhos
             else 'NAO DESENHOU - DoRendering nao foi chamado'))
    if estado == 4:
        print('  a heap nao coube ao lado do frontend. Diminua HEAP_BYTES no')
        print('  .cpp, ou use mesh_dump.mod, que troca a tela pela memoria.')
        return
    base, ln = dw(b + 24), dw(b + 28)
    print('  heap               %08X  base %08X  %d bytes' % (dw(b + 20), base, ln))
    rodando = dw(b + 36)
    if rodando:
        print('  TRAVOU dentro do modelo %d' % (rodando - 1))
    n = dw(b + 32)
    print('  modelos            %d' % n)
    for i in range(min(n, 10)):
        m = b + 40 + i * 20
        obj = dw(m + 4)
        if obj:
            print('    [%d] obj %08X  grupos %d  matidx %08X  geom %08X'
                  % (i, obj, dw(m + 8), dw(m + 12), dw(m + 16)))
        else:
            print('    [%d] FALHOU (nome nao resolveu, ou sem memoria)' % i)
    if base and estado == 3:
        print('  agora: salve um savestate e rode')
        print('    python mc3_pckbuild.py build <savestate> --base %08X --len %d'
              % (base, ln))

def _dump_report(d, dw):
    """mesh_dump.mod: what the frontend unload returned and where the private
    heap with the built meshes ended up.

    The two free figures are the whole point of the module: if they are equal,
    the frontend was not what was holding the memory, and no amount of
    unloading it will help."""
    M = MODREPORT
    estado = {0: 'waiting', 1: 'frontend unloaded', 2: 'NO HEAP - would not fit',
              3: 'building', 4: 'done'}
    print('   --- mesh dump (mesh_dump.mod) ---')
    print('   state             = %s   (%d frames)'
          % (estado.get(dw(M + 8), '?%d' % dw(M + 8)), dw(M + 4)))
    antes, depois = dw(M + 12), dw(M + 16)
    print('   main heap free    = %d before, %d after the unload' % (antes, depois))
    if depois > antes:
        print('                       the frontend gave back %d bytes (%.2f MB)'
              % (depois - antes, (depois - antes) / 1048576.0))
    elif dw(M + 8) >= 1:
        print('                       UNCHANGED - the frontend was not holding it')
    if dw(M + 8) == 2:
        print('   -> CreateMemTopHeap refused. Ask for less than the free figure '
              'above, or unload more than layer 8.')
        return
    print('   private heap      = %08X   base %08X   len %d'
          % (dw(M + 20), dw(M + 24), dw(M + 28)))
    print('   models built      = %d' % dw(M + 32))
    if dw(M + 36):
        print('   -> HUNG while building model %d: that name is the one that '
              'did it' % dw(M + 36))
    for i in range(dw(M + 32)):
        E = M + 40 + 20 * i
        print('     [%d] obj %08X  groups %-3d  matidx %08X  geom %08X  name %08X'
              % (i, dw(E + 4), dw(E + 8), dw(E + 12), dw(E + 16), dw(E)))
    if dw(M + 8) == 4 and dw(M + 24):
        print('   -> dump %08X..%08X from the savestate: every pointer in the '
              'graph falls inside it' % (dw(M + 24), dw(M + 24) + dw(M + 28)))

def _city_report(d, dw):
    """The table city_slot6.mod leaves at MODREPORT.

    Prints all six records, not just the one that was written. The question the
    mod exists to answer is "a new city WITHOUT touching the others", and the
    only honest way to show that is the whole table.
    """
    def cstr(addr, limite=64):
        if not addr:
            return ''
        o = addr & 0x1FFFFFF
        fim = d.find(bytes(1), o, o + limite)
        return d[o:fim if fim > 0 else o + limite].decode('latin1', 'replace')

    ST = {0: 'nothing happened', 1: 'registered', 2: 'REFUSED - slot 5 was not empty',
          3: 'the city array pointer was not valid'}
    st = dw(MODREPORT + 16)
    print('   --- city registry (city_slot6.mod) ---')
    print('   handler ran       = %d' % dw(MODREPORT + 4))
    print('   record array      = %08X   slot 5 at %08X'
          % (dw(MODREPORT + 8), dw(MODREPORT + 12)))
    print('   status            = %s' % ST.get(st, '? %d' % st))
    print('   slot 5 held       = %r (hoods %d)'
          % (cstr(dw(MODREPORT + 20)),
             dw(MODREPORT + 24) - (1 << 32 if dw(MODREPORT + 24) >> 31 else 0)))
    print('   race data loaded  = %s'
          % ('yes' if dw(MODREPORT + 28) else 'NO - the second hook did not run'))
    if st in (0, 3):
        return

    T = MODREPORT + 32
    print('   %-3s %-11s %-8s %-6s %-7s %s'
          % ('#', 'name', 'code', 'hoods', 'races', 'hood names'))
    for i in range(6):
        e = T + i * 24
        nome, cod = cstr(dw(e)), cstr(dw(e + 4))
        n = dw(e + 8)
        if n >> 31:
            n -= 1 << 32
        arr = dw(e + 12)
        hoods = ''
        if arr and 0 < n <= 16:
            hoods = ' '.join(cstr(dw(arr + k * 4)) for k in range(n))
        print('   %-3d %-11r %-8r %-6d %-7d %s'
              % (i, nome, cod, n, dw(e + 16), hoods))

    if st != 1:
        return
    corridas = dw(T + 5 * 24 + 16)
    print('   -> slot 5 registered. The five shipped cities are listed above '
          'exactly as the game built them.')
    if corridas:
        # +0x14 is what the frontend tests with `bgtz` before it will show a
        # city at all. Non-zero here means tune/race/<name>.loc was found,
        # parsed, and counted - the whole chain, not just the registration.
        print('   -> and slot 5 has %d race(s): its .loc was read. The gate the '
              'frontend uses is open.' % corridas)
    else:
        print('   -> but slot 5 has 0 races, so the frontend will still skip it. '
              'That is tune/race/<name>.loc not being found or not parsing.')


# --------------------------------------------------------------------------
def _probe_report(d, dw):
    """The report mc2_probe.mod leaves at MODREPORT.

    It answers one question - does MC3's text-mesh reader accept a Midnight
    Club 2 mesh - and the two MC3 files in the list are the control. A null for
    everything means the call was wrong, not that the format was rejected, and
    that distinction is the only reason the control is there.
    """
    def cstr(addr, limite=96):
        if not addr:
            return ''
        o = addr & 0x1FFFFFF
        fim = d.find(bytes(1), o, o + limite)
        return d[o:fim if fim > 0 else o + limite].decode('latin1', 'replace')

    A = MODREPORT + 32
    n = dw(MODREPORT + 16)
    print('   --- rmcModel::Create on text meshes (mc2_probe.mod) ---')
    print('   ticks seen        = %d' % dw(MODREPORT + 4))
    if not dw(MODREPORT + 12):
        emandamento = dw(MODREPORT + 8)
        if emandamento:
            print('   STOPPED INSIDE attempt %d - the game did not come back '
                  'from that Create' % emandamento)
        else:
            print('   not fired yet (waiting for the tick count in the module)')

    controle = mc2 = 0
    for i in range(6):
        e = A + i * 32
        nome = cstr(dw(e))
        if not nome:
            continue
        ret = dw(e + 4)
        marca = 'MC3' if 'checker' in nome else 'MC2'
        print('   [%s] %-36s -> %s'
              % (marca, nome, ('%08X' % ret) if ret else 'nothing'))
        if ret:
            corpo = ' '.join('%08X' % dw(e + 8 + w * 4) for w in range(6))
            print('        object: %s' % corpo)
            if marca == 'MC3':
                controle += 1
            else:
                mc2 += 1

    if n < 6:
        return
    if not controle:
        print('   -> the CONTROL failed too: this says nothing about MC2. '
              'Wrong name spelling, or Create needs something not set up here.')
    elif mc2:
        print('   -> MC3 loaded a Midnight Club 2 mesh. Same reader, no '
              'conversion.')
    else:
        print('   -> control loaded, MC2 did not: the reader is reachable and '
              'it refused these files.')


# --------------------------------------------------------------------------
def read_payload(path):
    b = io.open(path, 'rb').read()
    if len(b) % 4:
        b += bytes(4 - len(b) % 4)
    if len(b) > MAX_SIZE:
        raise SystemExit('payload of %d bytes does not fit in %d'
                         % (len(b), MAX_SIZE))
    return b


WHICH = """Midnight Club 3 modloader - one loader, three wrappings
======================================================

modloader_hostfs.pnach
    PCSX2 with HostFS. Put it in PCSX2/cheats/ and rename it so the name
    contains your ELF CRC. Needs "%s" next to the ELF.
    Rebuilding the payload does not touch this file.

modloader_embedded.pnach
    PCSX2 running from an ISO. Same as above, but the payload rides inside
    the file, so no loose file and no HostFS are needed. Rebuilding the
    payload means regenerating this file.

SLUS_213.55_iso.cht
    Real PS2 through OPL, RECOMMENDED. Put it in OPL's CHT folder and put
    mc3mod.bin in the rebuilt image as MC3MOD.BIN. ~70 codes, because the
    payload is a file rather than 650 codes of inline data. Rebuilding the
    payload does not touch this file.

SLUS_213.55_embedded.cht
    Real PS2 through OPL with no image change, at the cost of ~720 codes -
    over the limit several OPL builds enforce. Try it if you cannot rebuild
    the image; if OPL truncates the list, use the _iso one.

All four carry the SAME loader and the SAME payload, so any difference you
observe between emulator and hardware is a difference in the platform, not in
what was injected.
"""


def write_all(outdir, embed, path=PATH, early=False):
    """Every route at once, so emulator and real hardware can be compared.

    The three share one loader, byte for byte. Only the wrapping differs, and
    that is what makes the comparison worth anything: a behaviour difference
    between PCSX2 and a real PS2 cannot be blamed on the injected code differing.
    """
    if not os.path.isdir(outdir):
        os.makedirs(outdir)
    print('writing every route to %s' % outdir)
    made = [write_pnach(os.path.join(outdir, 'modloader_hostfs.pnach'), None, path, early),
            write_pnach(os.path.join(outdir, 'modloader_embedded.pnach'), embed, path, early),
            write_cht(os.path.join(outdir, 'SLUS_213.55_iso.cht'), None, PATH_ISO, early),
            write_cht(os.path.join(outdir, 'SLUS_213.55_embedded.cht'), embed, PATH_ISO, early)]
    guide = os.path.join(outdir, 'WHICH-FILE.txt')
    io.open(guide, 'w', encoding='latin1').write(WHICH % path)
    print('  %-32s what each file is for' % os.path.basename(guide))
    made.append(guide)
    return made


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.strip().splitlines()[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', choices=('build', 'pnach', 'cht', 'all',
                                       'dump', 'state'))
    ap.add_argument('args', nargs='*')
    ap.add_argument('--out')
    ap.add_argument('--outdir',
                    help='destination for `all` (default tools/output/modloader)')
    ap.add_argument('--embed', metavar='PAYLOAD.bin',
                    help='carry the payload inside the patch file instead of '
                         'reading it from disk (drops the HostFS requirement)')
    ap.add_argument('--path', metavar='DEVICE:/FILE',
                    help='path the loader opens (default %s). On real hardware '
                         'through OPL, USB storage is usually mass0:' % PATH)
    ap.add_argument('--early', action='store_true',
                    help='also install the boot stub at the end of crt0, so the '
                         'patch groups are applied before the game starts. The '
                         'only way to get startup semantics through OPL.')
    a = ap.parse_args()
    embed = read_payload(a.embed) if a.embed else None

    if a.action == 'build':
        w = [MAGIC] + demo()
        io.open(a.out, 'wb').write(struct.pack('<%dI' % len(w), *w))
        print('%s: proof-of-life payload, %d instructions' % (a.out, len(w)))
        print('   writes "MC3" at %08X and counts calls at %08X'
              % (PAYLOAD + 0x100, PAYLOAD + 0x104))
    elif a.action == 'pnach':
        write_pnach(a.out, embed, a.path or PATH, a.early)
    elif a.action == 'cht':
        write_cht(a.out, embed, a.path or PATH_ISO, a.early)
    elif a.action == 'all':
        if embed is None:
            raise SystemExit('all requires --embed PAYLOAD.bin: two of the three '
                             'routes carry the payload inside the patch file')
        write_all(a.outdir or DEFAULT_OUTDIR, embed, a.path or PATH, a.early)
    elif a.action == 'state':
        for p in a.args:
            report_state(p)
            print()
    else:
        for i, w in enumerate(loader()):
            print('%08X  %08X' % (CAVE + 4 * i, w))


if __name__ == '__main__':
    main()
