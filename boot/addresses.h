// GERADO por gera_addresses.py -- nao edite a mao.
// Endereco algum e copiado a mao: tudo sai de mc3_inject.py e do proprio ELF.
#ifndef MC3_ADDRESSES_H
#define MC3_ADDRESSES_H

// --- a imagem do jogo, lida do ELF ---
#define GAME_ENTRY      0x001A0008u
#define GAME_FILE_OFF   0x00000080u
#define GAME_VA         0x001A0000u
#define GAME_FILESZ     0x004D7074u
#define GAME_MEMSZ      0x00575D3Cu

// --- o mapa da injecao, de mc3_inject.py ---
#define MC3_CAVE        0x0061C858u
#define MC3_NAME        0x0061C960u
#define MC3_STATE       0x0061C980u
#define MC3_PAYLOAD     0x0061D000u
#define MC3_MAX_SIZE    0x00002800u
#define MC3_MAGIC       0x4D433350u
#define MC3_CFG         0x0061C9F0u
#define MC3_CFG_MAGIC   0x4D433343u

// bits de MC3_CFG+4, lidos pelo payload
#define MC3_F_DYNAMIC_PEDS       0x00000001u
#define MC3_F_DYNAMIC_CITY_RATE  0x00000002u

// The in-image loader, assembled by mc3_inject.py. Written verbatim so the
// four hooks below have something to call; with the payload already in RAM
// it only has to see state == 2 and jump.
static const unsigned int mc3_cave_code[] = {
    0x27BDFFC0u, 0xFFBF0030u, 0xFFB00028u, 0xFFB10020u, 0x0C10CD24u, 0x00000000u,
    0xAFA20018u, 0x3C100062u, 0x2610C980u, 0x8E080000u, 0x1500001Fu, 0x00000000u,
    0x24080001u, 0xAE080000u, 0x3C040062u, 0x2484C960u, 0x0C0E647Cu, 0x24050001u,
    0xAE020004u, 0x10400016u, 0x00000000u, 0x0040882Du, 0x0C0E65EAu, 0x0220202Du,
    0xAE020008u, 0x0040302Du, 0x3C080000u, 0x25082800u, 0x0106482Bu, 0x11200002u,
    0x00000000u, 0x0100302Du, 0x0220202Du, 0x3C050062u, 0x24A5D000u, 0x0C0E64EAu,
    0x00000000u, 0xAE02000Cu, 0x0C0E65D2u, 0x0220202Du, 0x24080002u, 0xAE080000u,
    0x8E080000u, 0x24090002u, 0x1509000Au, 0x00000000u, 0x3C080062u, 0x2508D000u,
    0x8D090000u, 0x3C034D43u, 0x24633350u, 0x15230003u, 0x00000000u, 0x0C187401u,
    0x00000000u, 0x8FA20018u, 0xDFBF0030u, 0xDFB00028u, 0xDFB10020u, 0x03E00008u,
    0x27BD0040u,
};
#define MC3_CAVE_WORDS  61

// --- a lista de adiados e a janela de relatorio dos mods ---
#define MC3_DEFER       0x0061CAE0u
#define MC3_DEFER_MAGIC 0x4D433344u
// First byte the defer list may NOT use: the report window starts here.
#define MC3_DEFER_END   0x0061CF00u
#define MC3_MODREPORT   0x0061CF00u

// --- multi-modulo ---
#define MC3_DISPATCH    0x0061D004u
#define MC3_MODTAB      0x0061D080u
#define MC3_MODBASE     0x0061D100u
#define MC3_MODEND      0x0061F858u
#define MC3_CAVE_END    0x0061F858u
#define MC3_MOD_MAGIC   0x4D43334Du

// --- shims de hook: 24 bytes por sitio, crescendo do topo do arena para baixo ---
#define MC3_SHIMHDR     0x0061D050u
#define MC3_SHIM_MAGIC  0x4D433353u
#define MC3_SHIM_STRIDE 24u

// O despachante: chama cada entrada da tabela. Fica em PAYLOAD+4, que e para
// onde o carregador do cave sempre pula - assim as rotas por pnach nao mudam.
static const unsigned int mc3_dispatch_code[] = {
    0x27BDFFE0u, 0xFFBF0000u, 0xFFB00008u, 0x3C100062u, 0x2610D080u, 0x8E080000u,
    0x11000006u, 0x00000000u, 0x0100F809u, 0x00000000u, 0x26100004u, 0x1000FFF9u,
    0x00000000u, 0xDFBF0000u, 0xDFB00008u, 0x27BD0020u, 0x03E00008u, 0x00000000u,
};
#define MC3_DISPATCH_WORDS  18

static const unsigned int mc3_hooks[] = {
    0x001A3164u, 0x001A32C8u, 0x001A353Cu, 0x001A385Cu,
};
#define MC3_HOOK_WORDS  4
#define MC3_HOOK_JAL    0x0C187216u

#endif
