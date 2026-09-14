import io
import os
import sys
import struct

# Everything is relative to this file. The toolkit directory gets renamed - it
# already has been - and an absolute path here would rot silently the first time
# that happens, which is the worst way for a generator to fail.
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(AQUI))
import mc3_inject as M

# The game executable, still an absolute path because it lives outside the
# toolkit. Override with the first argument.
ELF = sys.argv[1] if len(sys.argv) > 1 else r'Y:\MC3HostFS\slus_213.55.ELF'

elf = open(ELF, 'rb').read()
entry = struct.unpack_from('<I', elf, 0x18)[0]
po, pn, ps = (struct.unpack_from('<I', elf, 0x1C)[0],
              struct.unpack_from('<H', elf, 0x2C)[0],
              struct.unpack_from('<H', elf, 0x2A)[0])
segs = []
for i in range(pn):
    t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<IIIIIIII', elf, po + i * ps)
    if t == 1:
        segs.append((off, va, fsz, msz))
assert len(segs) == 1, 'mais de um PT_LOAD: o carregador precisa de um laco'
off, va, fsz, msz = segs[0]

L = M.loader()
out = []
W = out.append
W('// GERADO por gera_addresses.py -- nao edite a mao.')
W('// Endereco algum e copiado a mao: tudo sai de mc3_inject.py e do proprio ELF.')
W('#ifndef MC3_ADDRESSES_H')
W('#define MC3_ADDRESSES_H')
W('')
W('// --- a imagem do jogo, lida do ELF ---')
W('#define GAME_ENTRY      0x%08Xu' % entry)
W('#define GAME_FILE_OFF   0x%08Xu' % off)
W('#define GAME_VA         0x%08Xu' % va)
W('#define GAME_FILESZ     0x%08Xu' % fsz)
W('#define GAME_MEMSZ      0x%08Xu' % msz)
W('')
W('// --- o mapa da injecao, de mc3_inject.py ---')
W('#define MC3_CAVE        0x%08Xu' % M.CAVE)
W('#define MC3_NAME        0x%08Xu' % M.NAME)
W('#define MC3_STATE       0x%08Xu' % M.STATE)
W('#define MC3_PAYLOAD     0x%08Xu' % M.PAYLOAD)
W('#define MC3_MAX_SIZE    0x%08Xu' % M.MAX_SIZE)
W('#define MC3_MAGIC       0x%08Xu' % M.MAGIC)
W('#define MC3_CFG         0x%08Xu' % M.CFG)
W('#define MC3_CFG_MAGIC   0x%08Xu' % M.CFG_MAGIC)
W('')
W('// bits de MC3_CFG+4, lidos pelo payload')
W('#define MC3_F_DYNAMIC_PEDS       0x00000001u')
W('#define MC3_F_DYNAMIC_CITY_RATE  0x00000002u')
W('')
W('// The in-image loader, assembled by mc3_inject.py. Written verbatim so the')
W('// four hooks below have something to call; with the payload already in RAM')
W('// it only has to see state == 2 and jump.')
W('static const unsigned int mc3_cave_code[] = {')
for i in range(0, len(L), 6):
    W('    ' + ' '.join('0x%08Xu,' % w for w in L[i:i + 6]))
W('};')
W('#define MC3_CAVE_WORDS  %d' % len(L))
W('')
W('// --- a lista de adiados e a janela de relatorio dos mods ---')
W('#define MC3_DEFER       0x%08Xu' % M.DEFER)
W('#define MC3_DEFER_MAGIC 0x%08Xu' % M.DEFER_MAGIC)
W('// First byte the defer list may NOT use: the report window starts here.')
W('#define MC3_DEFER_END   0x%08Xu' % M.MODREPORT)
W('#define MC3_MODREPORT   0x%08Xu' % M.MODREPORT)
W('')
W('// --- multi-modulo ---')
W('#define MC3_DISPATCH    0x%08Xu' % M.DISPATCH)
W('#define MC3_MODTAB      0x%08Xu' % M.MODTAB)
W('#define MC3_MODBASE     0x%08Xu' % M.MODBASE)
W('#define MC3_MODEND      0x%08Xu' % M.MODEND)
W('#define MC3_CAVE_END    0x%08Xu' % M.CAVE_END)
W('#define MC3_MOD_MAGIC   0x4D43334Du')
W('')
W('// --- shims de hook: 24 bytes por sitio, crescendo do topo do arena para baixo ---')
W('#define MC3_SHIMHDR     0x%08Xu' % M.SHIMHDR)
W('#define MC3_SHIM_MAGIC  0x%08Xu' % M.SHIM_MAGIC)
W('#define MC3_SHIM_STRIDE 24u')
W('')
W('// O despachante: chama cada entrada da tabela. Fica em PAYLOAD+4, que e para')
W('// onde o carregador do cave sempre pula - assim as rotas por pnach nao mudam.')
D = M.dispatcher()
W('static const unsigned int mc3_dispatch_code[] = {')
for i in range(0, len(D), 6):
    W('    ' + ' '.join('0x%08Xu,' % w for w in D[i:i + 6]))
W('};')
W('#define MC3_DISPATCH_WORDS  %d' % len(D))
W('')
W('static const unsigned int mc3_hooks[] = {')
W('    ' + ' '.join('0x%08Xu,' % h for h in M.HOOKS))
W('};')
W('#define MC3_HOOK_WORDS  %d' % len(M.HOOKS))
W('#define MC3_HOOK_JAL    0x%08Xu' % (0x0C000000 | ((M.CAVE >> 2) & 0x3FFFFFF)))
W('')
W('#endif')
p = os.path.join(AQUI, 'addresses.h')
io.open(p, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('%s: entry %08X, imagem %08X +%08X (memsz %08X), loader %d palavras, %d hooks'
      % (os.path.basename(p), entry, va, fsz, msz, len(L), len(M.HOOKS)))
