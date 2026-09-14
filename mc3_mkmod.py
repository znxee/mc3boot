"""ELF -> .mod: a relocatable module the chain loader can place anywhere.

WHY THIS EXISTS

The payload is position dependent. It reaches its own constants with absolute
addresses (`lui v1,0x61` + `lwc1 $f1,OFFSET(v1)`), so a binary linked for
0x61D000 reads garbage anywhere else. With one payload that was fine - it always
went to the same place. With a modpack it is not: two mods from two authors
cannot both own 0x61D000, and nobody should need a compiler to add one.

So the linker is asked to keep its relocations (`ld -q`), and this turns them
into a short list the loader replays against whatever base it picked.

WHAT IT HANDLES, and why that is enough

Measured on the real payload: 44 R_MIPS_32, 4 R_MIPS_HI16, 4 R_MIPS_LO16, and
1 R_MIPS_26. Four types, and nothing exotic - no GOT, no TLS, no PLT, because
the modules are built -mno-abicalls and never call outside themselves.

HI16/LO16 are the awkward pair: on MIPS the value is split across two
instructions, and the low half is sign extended, so the high half carries a
correction. Pairing them is fiddly and easy to get subtly wrong. It is done HERE,
at build time, where it can be checked - the runtime just gets "these two
instructions form one address, add the delta".

FORMAT

    +0   'MC3M'
    +4   version
    +8   entry offset from the module base
    +12  code size
    +16  relocation count
    +20  link base (what it was linked at; the loader computes the delta)
    +24  hook table offset from the module base (0 if none)
    +28  hook count
    +32  code
         relocations, 12 bytes each: [type][off1][off2]

    type 1  word32     off1 = offset of a 32-bit absolute word
    type 2  jal26      off1 = offset of a j/jal
    type 3  hi_lo      off1 = the lui, off2 = its paired low half

The hook table is 8 bytes per entry: the address of a `jal` in the game, and the
handler to call instead. The handler pointer is an ordinary absolute address, so
it carries an R_MIPS_32 like anything else and the relocation pass above fixes it
with no special case - by the time the loader reads the table, it points at the
real thing.
"""
import io
import os
import struct
import sys

MAGIC = 0x4D43334D          # 'MC3M'
VERSION = 2
HDR = 32

R_MIPS_32 = 2
R_MIPS_26 = 4
R_MIPS_HI16 = 5
R_MIPS_LO16 = 6

T_WORD32 = 1
T_JAL26 = 2
T_HI_LO = 3


def _seg(d):
    """The single PT_LOAD: (vaddr, file offset, filesz, memsz)."""
    po = struct.unpack_from('<I', d, 0x1C)[0]
    pn = struct.unpack_from('<H', d, 0x2C)[0]
    ps = struct.unpack_from('<H', d, 0x2A)[0]
    out = []
    for i in range(pn):
        t, off, va, _pa, fsz, msz, _fl, _al = struct.unpack_from('<IIIIIIII', d, po + i * ps)
        if t == 1 and msz:
            out.append((va, off, fsz, msz))
    if len(out) != 1:
        raise SystemExit('esperava um PT_LOAD, achei %d' % len(out))
    return out[0]


def _sections(d):
    so = struct.unpack_from('<I', d, 0x20)[0]
    sn = struct.unpack_from('<H', d, 0x30)[0]
    ss = struct.unpack_from('<H', d, 0x2E)[0]
    shstr = struct.unpack_from('<H', d, 0x32)[0]
    hdrs = []
    for i in range(sn):
        b = so + i * ss
        name, typ, flags, addr, off, size, link, info, align, entsz = \
            struct.unpack_from('<IIIIIIIIII', d, b)
        hdrs.append(dict(name=name, type=typ, addr=addr, off=off, size=size,
                         link=link, info=info, entsz=entsz))
    strtab = hdrs[shstr]
    for h in hdrs:
        e = d.index(b'\0', strtab['off'] + h['name'])
        h['nome'] = d[strtab['off'] + h['name']:e].decode()
    return hdrs


def _relocs(d, hdrs, base):
    """[(offset no modulo, tipo ELF)] de todas as secoes REL."""
    out = []
    for h in hdrs:
        # n32 usa RELA (12 bytes, com addend explicito), nao REL. O addend nao
        # importa aqui: o binario ja esta ligado e a instrucao ja carrega o valor
        # final - so falta somar o delta.
        if h['type'] == 4:
            passo, campos = 12, '<III'
        elif h['type'] == 9:
            passo, campos = 8, '<II'
        else:
            continue
        for i in range(h['size'] // passo):
            vals = struct.unpack_from(campos, d, h['off'] + i * passo)
            off, info = vals[0], vals[1]
            out.append((off - base, info & 0xFF))
    out.sort()
    return out


def build(elf_path, out_path, entry_sym='payload_main'):
    d = open(elf_path, 'rb').read()
    va, off, fsz, msz = _seg(d)
    code = bytearray(d[off:off + fsz])
    if msz > fsz:
        code += bytes(msz - fsz)                # .bss viaja zerado

    hdrs = _sections(d)
    entry = struct.unpack_from('<I', d, 0x18)[0] - va

    brutas = _relocs(d, hdrs, va)
    dentro = [(o, t) for o, t in brutas if 0 <= o < len(code)]
    fora = len(brutas) - len(dentro)

    relocs, i = [], 0
    hi_pendente = []
    for o, t in dentro:
        if t == R_MIPS_32:
            relocs.append((T_WORD32, o, 0))
        elif t == R_MIPS_26:
            relocs.append((T_JAL26, o, 0))
        elif t == R_MIPS_HI16:
            hi_pendente.append(o)
        elif t == R_MIPS_LO16:
            # Um HI16 pode compartilhar o LO16 seguinte. Emparelhar cada HI
            # pendente com este LO reproduz a regra do linker sem heuristica.
            if not hi_pendente:
                # A LO16 with no HI16 waiting means GCC built one address in a
                # `lui` and then read SEVERAL fields through it. Only the first
                # low half would be relocated and the rest would keep their
                # link-time value - the module loads, runs, and reads the wrong
                # memory, which is the worst way for this to fail.
                #
                # Refusing here is not a limitation of the format so much as a
                # promise: if it builds, every address in it was moved. The fix
                # in the mod is to put the shared base behind a `noinline`
                # accessor, so each use gets its own pair. See done_slot() in
                # mods/core/core.cpp.
                raise SystemExit(
                    'LO16 sem HI16 em +%X: um `lui` compartilhado por varios '
                    'acessos. Ponha a base atras de um acessor noinline.' % o)
            for h in hi_pendente:
                relocs.append((T_HI_LO, h, o))
            hi_pendente = []
        else:
            raise SystemExit('relocacao nao suportada: tipo %d em +%X' % (t, o))
    if hi_pendente:
        raise SystemExit('%d HI16 sem LO16 correspondente' % len(hi_pendente))

    # alinhamento: o corpo comeca em HDR e as relocacoes logo apos o codigo
    while len(code) % 4:
        code += b'\0'

    ganchos = next((h for h in hdrs if h['nome'] == '.mc3hooks'), None)
    g_off = (ganchos['addr'] - va) if ganchos and ganchos['size'] else 0
    g_n = (ganchos['size'] // 8) if ganchos else 0
    if g_n and not (0 <= g_off < len(code)):
        raise SystemExit('.mc3hooks outside the loaded segment')

    hdr = struct.pack('<8I', MAGIC, VERSION, entry, len(code), len(relocs),
                      va, g_off, g_n)
    tab = b''.join(struct.pack('<3I', t, a, b) for t, a, b in relocs)
    io.open(out_path, 'wb').write(hdr + bytes(code) + tab)

    tipos = {}
    for t, _a, _b in relocs:
        tipos[t] = tipos.get(t, 0) + 1
    print('%s: %d bytes of code, entry +%X, linked at %08X'
          % (os.path.basename(out_path), len(code), entry, va))
    print('   relocations: %d  (word32 %d, jal26 %d, hi/lo %d)%s'
          % (len(relocs), tipos.get(T_WORD32, 0), tipos.get(T_JAL26, 0),
             tipos.get(T_HI_LO, 0),
             ('   %d outside the segment, ignored' % fora) if fora else ''))
    print('   file total: %d bytes' % (len(hdr) + len(code) + len(tab)))
    if g_n:
        print('   hooks: %d, table at +%X' % (g_n, g_off))
        for i in range(g_n):
            alvo, fn = struct.unpack_from('<II', code, g_off + i * 8)
            print('      %08X -> module +%X' % (alvo, fn - va))
    else:
        print('   hooks: none (per-frame callback only)')
    return out_path


def relocar(mod_bytes, novo_base):
    """Replica do que o carregador faz, para conferir no PC antes de bootar."""
    magic, ver, entry, tam, nrel, base, _r1, _r2 = struct.unpack_from('<8I', mod_bytes, 0)
    if magic != MAGIC:
        raise SystemExit('magic errado: %08X' % magic)
    code = bytearray(mod_bytes[HDR:HDR + tam])
    delta = (novo_base - base) & 0xFFFFFFFF
    p = HDR + tam
    for i in range(nrel):
        t, a, b = struct.unpack_from('<3I', mod_bytes, p + i * 12)
        if t == T_WORD32:
            v = struct.unpack_from('<I', code, a)[0]
            struct.pack_into('<I', code, a, (v + delta) & 0xFFFFFFFF)
        elif t == T_JAL26:
            w = struct.unpack_from('<I', code, a)[0]
            alvo = ((w & 0x3FFFFFF) << 2) | ((novo_base) & 0xF0000000)
            alvo = (alvo + delta) & 0xFFFFFFFF
            struct.pack_into('<I', code, a,
                             (w & 0xFC000000) | ((alvo >> 2) & 0x3FFFFFF))
        elif t == T_HI_LO:
            hi = struct.unpack_from('<I', code, a)[0]
            lo = struct.unpack_from('<I', code, b)[0]
            baixo = lo & 0xFFFF
            if baixo & 0x8000:
                baixo -= 0x10000
            valor = ((hi & 0xFFFF) << 16) + baixo
            valor = (valor + delta) & 0xFFFFFFFF
            novo_baixo = valor & 0xFFFF
            novo_alto = ((valor - (novo_baixo - (0x10000 if novo_baixo & 0x8000 else 0)))
                         >> 16) & 0xFFFF
            struct.pack_into('<I', code, a, (hi & 0xFFFF0000) | novo_alto)
            struct.pack_into('<I', code, b, (lo & 0xFFFF0000) | novo_baixo)
    return bytes(code), entry


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip().splitlines()[0])
        print('uso: mc3_mkmod.py ENTRADA.elf SAIDA.mod')
        return 1
    build(sys.argv[1], sys.argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main())
