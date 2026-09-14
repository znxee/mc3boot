"""
mc3_pnach.py - audit the pnach files PCSX2 will load for one game

PCSX2 loads EVERY file whose name matches the CRC, so a game with six pnach
files has six sets of patches fighting over the same memory with no warning if
they disagree. Nothing in the emulator tells you that two files write different
values to the same word, or that a line is dead because a later one overwrites
it, or that a `patch=` mode it does not understand was silently dropped.

This reads them all, resolves them the way PCSX2 would, and checks each line
against the ELF it claims to patch.

    python mc3_pnach.py --dir X:/Documents/PCSX2/cheats --elf Y:/MC3HostFS/slus_213.55.ELF

What it reports:

  CONFLITO   two files write different values to the same address
  MORTA      a line another line later overrides, so it never takes effect
  MODO       a `patch=N` outside the three PCSX2 implements
  FORA       an address outside the loaded image, so it patches nothing
  DESCRICAO  the comment says one value, the word encodes another

The last one is the reason this exists. A pnach comment is the only record of
what a patch was FOR, and a copy-pasted line keeps the old comment while
carrying a new value - the mismatch is invisible in the file and obvious once
the float is decoded.
"""

import argparse
import collections
import io
import os
import re
import struct

LINHA = re.compile(r'^\s*patch\s*=\s*(\d+)\s*,\s*(\w+)\s*,\s*([0-9A-Fa-f]+)\s*,'
                   r'\s*(\w+)\s*,\s*([0-9A-Fa-f]+)', re.I)
GRUPO = re.compile(r'^\s*\[(.+?)\]\s*$')
# numeros que uma descricao pode citar: 512, 1000, 0.35, 1.0, 150 ...
HEX = re.compile(r'0[xX][0-9A-Fa-f]+')
NUM = re.compile(r'(?<![A-Za-z0-9_.])(-?\d+(?:\.\d+)?)(?![A-Za-z0-9_])')
# "1/30" e um numero so, nao dois: sem isto o teste acusa a propria descricao certa
FRAC = re.compile(r'(-?\d+(?:\.\d+)?)\s*/\s*(\d+(?:\.\d+)?)')


def numeros(txt):
    """Todos os valores que um texto cita, com fracoes ja resolvidas.

    `512x512` e `1024x1024` sao a forma normal de escrever tamanho de textura, e
    o `x` cola o digito numa letra - sem separar aqui, o extrator descarta os dois
    numeros e a linha passa sem ser conferida.
    """
    # endereco em hex nao e numero citado; sem tirar antes, o `0x` vira `0`
    txt = HEX.sub(' ', txt)
    txt = re.sub(r'(?<=\d)\s*[xX*]\s*(?=\d)', ' ', txt)
    vals, resto = [], txt
    for m in FRAC.finditer(txt):
        a_, b_ = float(m.group(1)), float(m.group(2))
        if b_:
            vals.append(a_ / b_)
        resto = resto.replace(m.group(0), ' ')
    vals += [float(x) for x in NUM.findall(resto)]
    return vals

TAM = {'byte': 1, 'short': 2, 'word': 4, 'extended': 4, 'double': 8}

# o primeiro campo do `patch=` e QUANDO aplicar, e sao tres, nao dois:
MODOS = {0: 'On Startup', 1: 'Every Frame', 2: 'On Startup & Every Frame'}


def f32(w):
    return struct.unpack('<f', struct.pack('<I', w & 0xFFFFFFFF))[0]


def segmentos(d):
    ph = struct.unpack_from('<I', d, 0x1C)[0]
    phes = struct.unpack_from('<H', d, 0x2A)[0]
    phn = struct.unpack_from('<H', d, 0x2C)[0]
    out = []
    for i in range(phn):
        t, off, va, _pa, fsz, _msz = struct.unpack_from('<IIIIII', d, ph + i * phes)
        if t == 1 and fsz:
            out.append((va, off, fsz))
    return out


def le_elf(d, va, n):
    for base, off, tam in segmentos(d):
        if base <= va < base + tam:
            return int.from_bytes(d[off + va - base:off + va - base + n], 'little')
    return None


def parse(path):
    """[(modo, cpu, addr, tipo, valor, grupo, comentario, linha_no)]"""
    out, grupo, desc = [], '', ''
    for i, raw in enumerate(io.open(path, encoding='latin1'), 1):
        linha = raw.rstrip('\n')
        semcom = linha.split('//')[0]
        g = GRUPO.match(linha)
        if g:
            grupo, desc = g.group(1), ''
            continue
        if linha.lower().lstrip().startswith('description='):
            desc = linha.split('=', 1)[1].strip()
            continue
        # `;` e `//` comentam; PCSX2 ignora a linha inteira
        if linha.lstrip().startswith((';', '//')):
            continue
        m = LINHA.match(semcom)
        if not m:
            continue
        com = linha.split('//', 1)[1].strip() if '//' in linha else ''
        out.append((int(m.group(1)), m.group(2).upper(), int(m.group(3), 16),
                    m.group(4).lower(), int(m.group(5), 16), grupo,
                    (com + ' | ' + desc).strip(' |'), i, com))
    return out


def audit(pasta, elf, crc_filtro):
    d = open(elf, 'rb').read()
    c = 0
    for (w,) in struct.iter_unpack('<I', d[:len(d) // 4 * 4]):
        c ^= w
    print('ELF %s  crc %08X' % (os.path.basename(elf), c))
    alvo = crc_filtro or ('%08X' % c)
    print('procurando pnach para %s em %s\n' % (alvo, pasta))

    arquivos = []
    for nome in sorted(os.listdir(pasta)):
        if not nome.lower().endswith('.pnach'):
            continue
        if alvo.lower() not in nome.lower():
            continue
        arquivos.append(os.path.join(pasta, nome))
    if not arquivos:
        print('nenhum arquivo casa com o crc')
        return

    todos = []
    for f in arquivos:
        p = parse(f)
        print('   %-46s %3d patches, %d grupos'
              % (os.path.basename(f), len(p), len({x[5] for x in p if x[5]})))
        for x in p:
            todos.append((os.path.basename(f),) + x)
    print()

    problemas = collections.Counter()

    # --- modos que o PCSX2 nao implementa ---------------------------------
    for reg in todos:
        arq, modo = reg[0], reg[1]
        if modo not in MODOS:
            problemas['MODO'] += 1
            print('MODO       %s:%d  patch=%d nao existe (validos: %s) '
                  '-> a linha e IGNORADA'
                  % (arq, reg[8], modo,
                     ', '.join('%d=%s' % kv for kv in sorted(MODOS.items()))))

    # --- endereco fora da imagem carregada --------------------------------
    # Escrever fora do PT_LOAD nao e erro por si: e assim que se instala uma
    # CAVERNA de codigo (loader injetado em RAM livre). O que distingue as duas
    # coisas e a forma -- uma caverna e um bloco contiguo de palavras, um engano
    # de digitacao e um endereco solto. So o solto vira aviso.
    fora = sorted({reg[3] & 0x1FFFFFF for reg in todos
                   if le_elf(d, reg[3] & 0x1FFFFFF, 1) is None})
    blocos, ini, prev = [], None, None
    for va in fora:
        if prev is None or va != prev + 4:
            if ini is not None:
                blocos.append((ini, prev + 4 - ini))
            ini = va
        prev = va
    if ini is not None:
        blocos.append((ini, prev + 4 - ini))
    for base, n in blocos:
        if n >= 16:
            print('CAVERNA    %08X..%08X  %d palavras fora do PT_LOAD '
                  '(codigo/dados injetados)' % (base, base + n, n // 4))
        else:
            for va in range(base, base + n, 4):
                problemas['FORA'] += 1
                donos = [r for r in todos if (r[3] & 0x1FFFFFF) == va]
                print('FORA       %s:%d  %08X fora do PT_LOAD e fora de qualquer bloco'
                      % (donos[0][0], donos[0][8], va))

    # --- conflito: mesmo endereco, valores diferentes ---------------------
    def largura(tipo):
        return TAM.get(tipo, 4)

    porend = collections.defaultdict(list)
    for reg in todos:
        porend[(reg[3] & 0x1FFFFFF, largura(reg[4]))].append(reg)
    for (va, _n), regs in sorted(porend.items()):
        vals = {r[5] for r in regs}
        arqs = {r[0] for r in regs}
        if len(vals) > 1:
            # dois grupos do MESMO arquivo escrevendo valores diferentes nao e
            # conflito: sao opcoes alternativas, e quem escolhe e a caixinha do
            # PCSX2. So vira conflito de verdade dentro de um grupo, entre
            # arquivos, ou fora de qualquer grupo.
            grupos = {(r[0], r[6]) for r in regs}
            mesmo_arq = len({r[0] for r in regs}) == 1
            todos_agrupados = all(r[6] for r in regs)
            if mesmo_arq and todos_agrupados and len(grupos) == len(regs):
                problemas['EXCLUSIVO'] += 1
                print('EXCLUSIVO  %08X: %d grupos alternativos, marque so um'
                      % (va, len(regs)))
                for r in regs:
                    print('              [%s] valor %08X' % (r[6], r[5]))
                continue
            problemas['CONFLITO'] += 1
            print('CONFLITO   %08X recebe %d valores diferentes:' % (va, len(vals)))
            for r in regs:
                print('              %-46s:%-4d patch=%d valor %08X  %s'
                      % (r[0], r[8], r[1], r[5], r[7][:40]))
        elif len(regs) > 1 and len(arqs) > 1:
            problemas['DUPLICADA'] += 1
            print('DUPLICADA  %08X escrito por %d arquivos com o mesmo valor: %s'
                  % (va, len(arqs), ', '.join(sorted(arqs))))

    # --- linha morta: mesmo arquivo, mesmo endereco, mesmo modo -----------
    porarq = collections.defaultdict(list)
    for reg in todos:
        # o grupo entra na chave: linhas de grupos diferentes nunca coexistem
        porarq[(reg[0], reg[6], reg[1], reg[3] & 0x1FFFFFF)].append(reg)
    for (arq, _g, modo, va), regs in sorted(porarq.items()):
        if len(regs) > 1 and len({r[5] for r in regs}) > 1:
            problemas['MORTA'] += 1
            print('MORTA      %s: %08X escrito %dx no mesmo modo; so o ULTIMO vale'
                  % (arq, va, len(regs)))
            for r in regs:
                print('              linha %-4d valor %08X  %s' % (r[8], r[5], r[7][:44]))

    # --- valor no ELF ja e o que o patch quer? ----------------------------
    print()
    for reg in todos:
        arq, _m, _cpu, addr, tipo, val = reg[0], reg[1], reg[2], reg[3], reg[4], reg[5]
        n = TAM.get(tipo, 4)
        atual = le_elf(d, addr & 0x1FFFFFF, n)
        if atual is not None and atual == val:
            problemas['NOP'] += 1
            print('SEM EFEITO %s:%d  %08X ja vale %0*X no ELF'
                  % (arq, reg[8], addr, n * 2, val))

    # --- descricao cita um numero que o valor nao contem -------------------
    sozinhos = collections.Counter((r[0], r[6]) for r in todos if r[6])
    for reg in todos:
        # o comentario da linha sempre; a descricao do grupo SO quando o grupo tem
        # uma unica linha, porque ai ela descreve aquela palavra sem ambiguidade
        arq, addr, tipo, val = reg[0], reg[3], reg[4], reg[5]
        com = reg[9]
        if sozinhos.get((reg[0], reg[6])) == 1:
            com = (com + ' ' + reg[7]).strip()
        if tipo != 'word' or not com:
            continue
        # duas formas dao pra decodificar: `lui $at, imm` (metade alta de um float)
        # e `addiu $r, $zero, imm` (um inteiro literal, tipo o tamanho de textura)
        if (val >> 26) == 0x0F and ((val >> 16) & 0x1F) == 1:
            f = f32((val & 0xFFFF) << 16)
        elif (val >> 26) == 0x09 and ((val >> 21) & 0x1F) == 0:
            f = float(val & 0xFFFF)
        else:
            continue
        # "A -> B" quer dizer que o patch produz B; e o alvo que importa.
        # "(era X)" / "(was X)" cita o valor ANTIGO -- fora da conferencia.
        com = re.sub(r'\((?:era|was)\b[^)]*\)', ' ', com, flags=re.I)
        # A PRIMEIRA seta separa "de" e "para": tudo depois dela e o lado alvo.
        # Dividir na ultima perde o valor quando a descricao tem mais de uma seta
        # (ex.: "1.0 -> 2.0 ... (50/100 -> 100/200)").
        alvo_txt = com.split('->', 1)[1] if '->' in com else com
        citados = numeros(alvo_txt)
        if not citados:
            continue
        # aceita se algum numero citado bate com o float (ou com metade/dobro dele)
        ok = any(abs(f - c) < max(1e-4, abs(c) * 0.02) for c in citados)
        if not ok:
            problemas['DESCRICAO'] += 1
            print('DESCRICAO  %s:%d  %08X monta %g mas o texto cita %s'
                  % (arq, reg[8], addr, f, '/'.join('%g' % c for c in citados)))

    print()
    if problemas:
        print('resumo: ' + ', '.join('%s=%d' % kv for kv in sorted(problemas.items())))
    else:
        print('nenhum problema encontrado')



# ==========================================================================
#  GERADOR C++  --to-cpp
# ==========================================================================
#
#  Emite a tabela de escritas a partir dos .pnach REAIS, para o payload do
#  modloader aplicar. A ideia e nao ter endereco copiado na mao: o pnach
#  continua sendo a fonte, e o header e derivado.
#
#  O que NAO migra, e por que:
#
#    60A42FF5.pnach            bootstrap do HostFS. Roda ANTES do jogo abrir
#                              qualquer arquivo -- e o payload e justamente um
#                              arquivo lido por HostFS. Ovo e galinha.
#    *_modloader.pnach         e o carregador. Nao pode se carregar sozinho.
#
#  Fora isso, tudo que e `patch=1` vira escrita por quadro e o comportamento e
#  o mesmo. As linhas so-`patch=0` viram escrita tardia: o payload so comeca a
#  rodar dentro do mcGame, entao se o valor ja foi lido na inicializacao ele so
#  passa a valer na proxima vez que o jogo reler. Isso esta marcado em cada
#  grupo com `tarde`.

# nome EXATO (o bootstrap) e sufixos (o carregador). Substring solta pegava
# tambem o SLUS-21355_60A42FF5.pnach, que termina igual.
# O bootstrap e identificado pelo NOME DO GRUPO, nao pelo do arquivo. Ele ja
# morou em `60A42FF5.pnach` e hoje mora em `SLUS-21355_60A42FF5.pnach`; filtrar
# por arquivo quebrou silenciosamente quando isso mudou - o bootstrap vazou para
# a tabela do payload e o header do carregador saiu vazio.
BOOTSTRAP_GRUPO = 'hostfs'
# SUBSTRING, nao sufixo: `_modloader_embedded.pnach` nao termina em
# `_modloader.pnach` e entrou na tabela, fazendo o payload escrever o proprio
# carregador como se fosse patch -- 777 escritas em vez de 48.
EXCLUI_CONTEM = ('_modloader',)


def _slug(s):
    s = re.sub(r'[^A-Za-z0-9]+', '_', s).strip('_').upper()
    return re.sub(r'_+', '_', s) or 'SEM_NOME'


def to_cpp(pasta, elf, crc_filtro, saida, prefixo='MC3', so_bootstrap=False):
    """Emite a tabela de escritas.

    `so_bootstrap` inverte o filtro: em vez de pular o bootstrap do HostFS, emite
    SO ele. Isso existe por causa do chain loader. Enquanto a injecao era feita
    por pnach o bootstrap nao tinha como migrar - ele roda antes do jogo abrir
    qualquer arquivo, e o payload E um arquivo. O carregador desfaz esse no: ele
    le com o sistema de arquivos DELE e escreve na imagem antes do jogo comecar,
    entao ali sao escritas de memoria como quaisquer outras.

    Sai em header separado, com prefixo proprio, porque quem aplica e diferente:
    o bootstrap so faz sentido no carregador, e so quando o jogo veio de host0:.
    """
    d = open(elf, 'rb').read()
    c = 0
    for (w,) in struct.iter_unpack('<I', d[:len(d) // 4 * 4]):
        c ^= w
    alvo = crc_filtro or ('%08X' % c)

    grupos, ordem, pulados = {}, [], []
    for nome in sorted(os.listdir(pasta)):
        if not nome.lower().endswith('.pnach') or alvo.lower() not in nome.lower():
            continue
        if any(x.lower() in nome.lower() for x in EXCLUI_CONTEM):
            pulados.append(nome)          # o proprio carregador; nao se injeta
            continue
        for (modo, cpu, addr, tipo, val, grupo, _c, ln, com) in parse(os.path.join(pasta, nome)):
            if cpu != 'EE':
                continue
            g = grupo or os.path.splitext(nome)[0]
            if (g.lower().startswith(BOOTSTRAP_GRUPO)) != so_bootstrap:
                continue
            if g not in grupos:
                grupos[g] = []
                ordem.append(g)
            grupos[g].append((modo, addr, tipo, val, com, nome, ln))

    # um grupo por endereco -> exclusividade, para virar #error no header
    porend = collections.defaultdict(set)
    for g in ordem:
        for (_m, addr, _t, _v, _c, _n, _l) in grupos[g]:
            porend[addr & 0x01FFFFFF].add(g)
    exclusivos = set()
    for addr, gs in porend.items():
        if len(gs) > 1:
            exclusivos.add(frozenset(gs))

    # antes de qualquer filtragem: um grupo so e "tarde" se NENHUMA linha dele
    # for patch=1 ou patch=2
    tarde = {g: all(m == 0 for (m, *_r) in grupos[g]) for g in ordem}

    out, colapsadas = [], []
    W = out.append
    W('// GERADO por mc3_pnach.py --to-cpp -- nao edite a mao.')
    W('// Fonte: %s  (crc %s)' % (pasta, alvo))
    if pulados:
        W('// Fora de proposito (bootstrap/carregador): %s' % ', '.join(pulados))
    W('#ifndef %s_PATCHES_H' % prefixo)
    W('#define %s_PATCHES_H' % prefixo)
    W('')
    # typedef, nao `struct` nu: o payload e C++ mas o carregador de boot e C, e
    # em C `mc3_write x` sem o `struct` nao compila. Um header gerado tem de
    # servir aos dois consumidores.
    W('#ifndef MC3_WRITE_TYPES')
    W('#define MC3_WRITE_TYPES')
    W('typedef struct { unsigned int addr; unsigned int value;')
    W('                 unsigned char size; } mc3_write;')
    W('typedef struct { const char* name; const mc3_write* w; unsigned short n;')
    W('                 unsigned char tarde; unsigned char enabled; } mc3_group;')
    W('#endif')
    W('')

    for g in ordem:
        s = _slug(g)
        W('#ifndef %s_EN_%s' % (prefixo, s))
        W('#define %s_EN_%s 0' % (prefixo, s))
        W('#endif')
        # A group often carries the same write twice: `patch=0` to land it at
        # startup and `patch=1` to hold it every frame. That distinction is a
        # pnach concept - the payload writes on every pass regardless - so here
        # it is one write, not two. Left in, it would also inflate the `writes`
        # and `changed` counters that the state readout is meant to be read by.
        visto, itens = set(), []
        for it in grupos[g]:
            chave = (it[1] & 0x01FFFFFF, it[3], TAM.get(it[2], 4))
            if chave in visto:
                colapsadas.append((g, chave))
                continue
            visto.add(chave)
            itens.append(it)
        grupos[g] = itens

        W('static const mc3_write %s_w_%s[] = {' % (prefixo.lower(), s))
        for (modo, addr, tipo, val, com, nome, ln) in grupos[g]:
            n = TAM.get(tipo, 4)
            v = val & ((1 << (8 * n)) - 1)
            aviso = ''
            if v != val:
                # escrita parcial: PCSX2 trunca. E um idioma usado de proposito
                # (mexer so no byte baixo do imediato), mas so funciona se o
                # resto da palavra JA estiver com o valor certo -- entao aqui
                # isso e conferido no ELF, nao suposto.
                atual = le_elf(d, addr & 0x01FFFFFF, 4)
                if atual is None:
                    aviso = '   // ATENCAO: %X truncado para %X, endereco fora do ELF' % (val, v)
                else:
                    masc = (1 << (8 * n)) - 1
                    fim = (atual & ~masc) | v
                    quer = (atual & ~0xFFFF) | (val & 0xFFFF)
                    aviso = ('   // %X em %s -> %X; palavra %08X vira %08X  [%s]'
                             % (val, tipo, v, atual, fim,
                                'confere' if fim == quer else 'DIVERGE do pretendido %08X' % quer))
            W('    { 0x%08Xu, 0x%08Xu, %d },%s%s'
              % (addr & 0x01FFFFFF, v, n, aviso,
                 ('   // ' + com) if com and not aviso else ''))
        W('};')
        W('')

    W('static mc3_group %s_groups[] = {' % prefixo.lower())
    for g in ordem:
        s = _slug(g)
        W('    { "%s", %s_w_%s, %d, %d, %s_EN_%s },'
          % (g.replace(chr(92), '/'), prefixo.lower(), s, len(grupos[g]),
             1 if tarde[g] else 0, prefixo, s))
    W('};')
    W('static const int %s_NGROUPS = %d;' % (prefixo, len(ordem)))
    W('')

    for fs in sorted(exclusivos, key=lambda f: sorted(f)):
        ss = sorted(_slug(x) for x in fs)
        W('// mutuamente exclusivos: escrevem o mesmo endereco')
        W('#if (' + ' + '.join(prefixo + '_EN_' + x for x in ss) + ') > 1')
        W('#error "ligue so um: %s"' % ', '.join(ss))
        W('#endif')
        W('')

    W('#endif')
    txt = chr(10).join(out) + chr(10)
    io.open(saida, 'w', encoding='utf-8', newline=chr(10)).write(txt)
    print('%s: %d grupos, %d escritas, %d conjuntos exclusivos'
          % (saida, len(ordem), sum(len(v) for v in grupos.values()), len(exclusivos)))
    if colapsadas:
        print('   %d escritas repetidas colapsadas (patch=0 e patch=1 da mesma palavra):'
              % len(colapsadas))
        for g, (a, v, n) in colapsadas:
            print('      %-46s %08X = %08X' % (g, a, v))
    for g in ordem:
        print('   %s_EN_%-42s %2d%s' % (prefixo, _slug(g), len(grupos[g]),
                                         '   (tarde: so patch=0)' if tarde[g] else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[1],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dir', required=True)
    ap.add_argument('--elf', required=True)
    ap.add_argument('--crc')
    ap.add_argument('--to-cpp', metavar='SAIDA',
                    help='gera o header de patches para o payload do modloader')
    ap.add_argument('--to-cpp-boot', metavar='SAIDA',
                    help='gera o header do bootstrap de HostFS para o chain loader')
    a = ap.parse_args()
    if a.to_cpp or a.to_cpp_boot:
        if a.to_cpp:
            to_cpp(a.dir, a.elf, a.crc, a.to_cpp)
        if a.to_cpp_boot:
            to_cpp(a.dir, a.elf, a.crc, a.to_cpp_boot,
                   prefixo='MC3BOOT', so_bootstrap=True)
    else:
        audit(a.dir, a.elf, a.crc)


if __name__ == '__main__':
    main()
