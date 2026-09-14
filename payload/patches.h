// GERADO por mc3_pnach.py --to-cpp -- nao edite a mao.
// Fonte: C:/Users/rodri/AppData/Local/Temp/claude/C--MC3test-Claudio/b71d00d7-f8af-4003-afc5-68c0f6d11632/scratchpad/cheats_ours  (crc 60A42FF5)
#ifndef MC3_PATCHES_H
#define MC3_PATCHES_H

#ifndef MC3_WRITE_TYPES
#define MC3_WRITE_TYPES
typedef struct { unsigned int addr; unsigned int value;
                 unsigned char size; } mc3_write;
typedef struct { const char* name; const mc3_write* w; unsigned short n;
                 unsigned char tarde; unsigned char enabled; } mc3_group;
#endif

#ifndef MC3_EN_GRAPHICAL_REFLEXO_QUALIDADE_ALTA
#define MC3_EN_GRAPHICAL_REFLEXO_QUALIDADE_ALTA 0
#endif
static const mc3_write mc3_w_GRAPHICAL_REFLEXO_QUALIDADE_ALTA[] = {
    { 0x005613CCu, 0x240C0000u, 4 },
};

#ifndef MC3_EN_GRAPHICAL_REFLEXO_MAPA_512X512
#define MC3_EN_GRAPHICAL_REFLEXO_MAPA_512X512 0
#endif
static const mc3_write mc3_w_GRAPHICAL_REFLEXO_MAPA_512X512[] = {
    { 0x0056157Cu, 0x24020200u, 4 },   // +0x20 largura 256 -> 512
    { 0x00561588u, 0x24020200u, 4 },   // +0x24 altura  256 -> 512
    { 0x00561598u, 0x24080200u, 4 },   // altura literal do "EnvMap Color"
};

#ifndef MC3_EN_GRAPHICAL_REFLEXO_FOV_150_CIDADE
#define MC3_EN_GRAPHICAL_REFLEXO_FOV_150_CIDADE 0
#endif
static const mc3_write mc3_w_GRAPHICAL_REFLEXO_FOV_150_CIDADE[] = {
    { 0x005616B8u, 0x3C014316u, 4 },
};

#ifndef MC3_EN_GRAPHICAL_REFLEXO_FOV_150_GARAGEM
#define MC3_EN_GRAPHICAL_REFLEXO_FOV_150_GARAGEM 0
#endif
static const mc3_write mc3_w_GRAPHICAL_REFLEXO_FOV_150_GARAGEM[] = {
    { 0x00561668u, 0x3C014316u, 4 },
};

#ifndef MC3_EN_GRAPHICAL_REFLEXO_FAR_PLANE_2000
#define MC3_EN_GRAPHICAL_REFLEXO_FAR_PLANE_2000 0
#endif
static const mc3_write mc3_w_GRAPHICAL_REFLEXO_FAR_PLANE_2000[] = {
    { 0x00562588u, 0x3C0144FAu, 4 },
};

#ifndef MC3_EN_GRAPHICAL_REFLEXO_INTENSIDADE_0_50
#define MC3_EN_GRAPHICAL_REFLEXO_INTENSIDADE_0_50 0
#endif
static const mc3_write mc3_w_GRAPHICAL_REFLEXO_INTENSIDADE_0_50[] = {
    { 0x00561658u, 0x3C013F00u, 4 },   // garagem   0.374 -> 0.50
    { 0x0056165Cu, 0x34210000u, 4 },
    { 0x00561694u, 0x3C013F00u, 4 },   // cidade A  0.150 -> 0.50
    { 0x00561698u, 0x34210000u, 4 },
    { 0x005616A8u, 0x3C013F00u, 4 },   // cidade B  0.374 -> 0.50
    { 0x005616ACu, 0x34210000u, 4 },
};

#ifndef MC3_EN_60_FPS_1_DESTRAVAR_PARA_60_FPS
#define MC3_EN_60_FPS_1_DESTRAVAR_PARA_60_FPS 0
#endif
static const mc3_write mc3_w_60_FPS_1_DESTRAVAR_PARA_60_FPS[] = {
    { 0x005280F0u, 0x00000000u, 4 },
};

#ifndef MC3_EN_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS
#define MC3_EN_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS 0
#endif
static const mc3_write mc3_w_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS[] = {
    { 0x001AC2A4u, 0x3C013C88u, 4 },   // 0.033333 -> 0.016667   (1/30 -> 1/60)
    { 0x0028EE68u, 0x3C013C87u, 4 },   // 0.0330   -> 0.0165
};

#ifndef MC3_EN_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS
#define MC3_EN_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS 0
#endif
static const mc3_write mc3_w_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS[] = {
    { 0x001AC2A4u, 0x3C013C08u, 4 },   // 0.033333 -> 0.008333   (1/30 -> 1/120)
    { 0x0028EE68u, 0x3C013C07u, 4 },   // 0.0330   -> 0.00825
};

#ifndef MC3_EN_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS
#define MC3_EN_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS 0
#endif
static const mc3_write mc3_w_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS[] = {
    { 0x0045F638u, 0x3C013E33u, 4 },   // 0.35 -> 0.175   amplitude aleatoria
    { 0x0045F644u, 0x3C013F0Cu, 4 },   // 1.10 -> 0.55    taxa base
    { 0x0045F698u, 0x3C013F00u, 4 },   // 1.0  -> 0.5     taxa sem variacao
    { 0x0045F6A8u, 0x3C013F00u, 4 },   // 1.0  -> 0.5     2o argumento do SetPlayBackRate
    { 0x0045FD98u, 0x3C013F00u, 4 },   // 1.0  -> 0.5     chamadas em mcCreature::ApplyAnimRate
    { 0x0045FDB0u, 0x3C013F00u, 4 },
    { 0x0045FDC8u, 0x3C013F00u, 4 },
    { 0x00463430u, 0x3C013F00u, 4 },   // 1.0  -> 0.5     defaults internos do SetPlayBackRate
    { 0x0046345Cu, 0x3C013F00u, 4 },
    { 0x004633A0u, 0x3C013F00u, 4 },   // 1.0  -> 0.5     construtor do mcCreatureAnimator
};

#ifndef MC3_EN_CIDADE_ORCAMENTO_PARA_60_FPS
#define MC3_EN_CIDADE_ORCAMENTO_PARA_60_FPS 0
#endif
static const mc3_write mc3_w_CIDADE_ORCAMENTO_PARA_60_FPS[] = {
    { 0x001A70F0u, 0x2405003Cu, 4 },
};

#ifndef MC3_EN_CIDADE_ORCAMENTO_PARA_90_FPS
#define MC3_EN_CIDADE_ORCAMENTO_PARA_90_FPS 0
#endif
static const mc3_write mc3_w_CIDADE_ORCAMENTO_PARA_90_FPS[] = {
    { 0x001A70F0u, 0x2405005Au, 4 },
};

#ifndef MC3_EN_CIDADE_ORCAMENTO_PARA_120_FPS
#define MC3_EN_CIDADE_ORCAMENTO_PARA_120_FPS 0
#endif
static const mc3_write mc3_w_CIDADE_ORCAMENTO_PARA_120_FPS[] = {
    { 0x001A70F0u, 0x24050078u, 4 },
};

#ifndef MC3_EN_CIDADE_ORCAMENTO_PARA_240_FPS
#define MC3_EN_CIDADE_ORCAMENTO_PARA_240_FPS 0
#endif
static const mc3_write mc3_w_CIDADE_ORCAMENTO_PARA_240_FPS[] = {
    { 0x001A70F0u, 0x240500F0u, 4 },
};

#ifndef MC3_EN_CIDADE_ORCAMENTO_PARA_1_FPS
#define MC3_EN_CIDADE_ORCAMENTO_PARA_1_FPS 0
#endif
static const mc3_write mc3_w_CIDADE_ORCAMENTO_PARA_1_FPS[] = {
    { 0x001A70F0u, 0x24050001u, 4 },
};

#ifndef MC3_EN_TRAFEGO_DISTANCIA_DE_DESENHO_X2
#define MC3_EN_TRAFEGO_DISTANCIA_DE_DESENHO_X2 0
#endif
static const mc3_write mc3_w_TRAFEGO_DISTANCIA_DE_DESENHO_X2[] = {
    { 0x0021DDD4u, 0x3C014000u, 4 },
};

#ifndef MC3_EN_VIDEO_WIDESCREEN_16_9
#define MC3_EN_VIDEO_WIDESCREEN_16_9 0
#endif
static const mc3_write mc3_w_VIDEO_WIDESCREEN_16_9[] = {
    { 0x00527E14u, 0x3C013FE3u, 4 },
    { 0x00527E18u, 0x34218E34u, 4 },
};

#ifndef MC3_EN_VIDEO_MODO_640X448
#define MC3_EN_VIDEO_MODO_640X448 0
#endif
static const mc3_write mc3_w_VIDEO_MODO_640X448[] = {
    { 0x004BF458u, 0x00000001u, 1 },
};

#ifndef MC3_EN_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL
#define MC3_EN_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL 0
#endif
static const mc3_write mc3_w_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL[] = {
    { 0x004BF458u, 0x00000000u, 1 },
    { 0x001D6DACu, 0x000000E0u, 1 },   // 1E0 em byte -> E0; palavra 240501C0 vira 240501E0  [confere]
    { 0x001D6DB0u, 0x00000080u, 1 },   // 280 em byte -> 80; palavra 24040200 vira 24040280  [confere]
};

#ifndef MC3_EN_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA
#define MC3_EN_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA 0
#endif
static const mc3_write mc3_w_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA[] = {
    { 0x001D6DB0u, 0x00000080u, 1 },   // 280 em byte -> 80; palavra 24040200 vira 24040280  [confere]
    { 0x001C8310u, 0x00000080u, 1 },   // 280 em byte -> 80; palavra 24070200 vira 24070280  [confere]
    { 0x001C8314u, 0x000000E0u, 1 },   // 1E0 em byte -> E0; palavra 240801C0 vira 240801E0  [confere]
    { 0x001C829Cu, 0x00000000u, 4 },   // desativa o corte do glow "HDR"
    { 0x001C82ACu, 0x00000000u, 4 },   // desativa o corte do glow "HDR"
    { 0x001C828Cu, 0x241E0280u, 4 },   // deslocamento do glow "HDR"
};

#ifndef MC3_EN_VIDEO_DESATIVAR_MOTION_BLUR
#define MC3_EN_VIDEO_DESATIVAR_MOTION_BLUR 0
#endif
static const mc3_write mc3_w_VIDEO_DESATIVAR_MOTION_BLUR[] = {
    { 0x001CA488u, 0x03E00008u, 4 },
};

static mc3_group mc3_groups[] = {
    { "Graphical/Reflexo - Qualidade alta", mc3_w_GRAPHICAL_REFLEXO_QUALIDADE_ALTA, 1, 0, MC3_EN_GRAPHICAL_REFLEXO_QUALIDADE_ALTA },
    { "Graphical/Reflexo - Mapa 512x512", mc3_w_GRAPHICAL_REFLEXO_MAPA_512X512, 3, 0, MC3_EN_GRAPHICAL_REFLEXO_MAPA_512X512 },
    { "Graphical/Reflexo - FOV 150 (cidade)", mc3_w_GRAPHICAL_REFLEXO_FOV_150_CIDADE, 1, 0, MC3_EN_GRAPHICAL_REFLEXO_FOV_150_CIDADE },
    { "Graphical/Reflexo - FOV 150 (garagem)", mc3_w_GRAPHICAL_REFLEXO_FOV_150_GARAGEM, 1, 0, MC3_EN_GRAPHICAL_REFLEXO_FOV_150_GARAGEM },
    { "Graphical/Reflexo - Far plane 2000", mc3_w_GRAPHICAL_REFLEXO_FAR_PLANE_2000, 1, 0, MC3_EN_GRAPHICAL_REFLEXO_FAR_PLANE_2000 },
    { "Graphical/Reflexo - Intensidade 0.50", mc3_w_GRAPHICAL_REFLEXO_INTENSIDADE_0_50, 6, 0, MC3_EN_GRAPHICAL_REFLEXO_INTENSIDADE_0_50 },
    { "60 FPS/1 - Destravar para 60 fps", mc3_w_60_FPS_1_DESTRAVAR_PARA_60_FPS, 1, 0, MC3_EN_60_FPS_1_DESTRAVAR_PARA_60_FPS },
    { "60 FPS/2a - Flash da tela de loading @ 60 fps", mc3_w_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS, 2, 0, MC3_EN_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS },
    { "60 FPS/2b - Flash da tela de loading @ 120 fps", mc3_w_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS, 2, 0, MC3_EN_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS },
    { "60 FPS/3 - Pedestres (constante, SO para 60 fps)", mc3_w_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS, 10, 0, MC3_EN_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS },
    { "Cidade/Orcamento para 60 fps", mc3_w_CIDADE_ORCAMENTO_PARA_60_FPS, 1, 0, MC3_EN_CIDADE_ORCAMENTO_PARA_60_FPS },
    { "Cidade/Orcamento para 90 fps", mc3_w_CIDADE_ORCAMENTO_PARA_90_FPS, 1, 0, MC3_EN_CIDADE_ORCAMENTO_PARA_90_FPS },
    { "Cidade/Orcamento para 120 fps", mc3_w_CIDADE_ORCAMENTO_PARA_120_FPS, 1, 0, MC3_EN_CIDADE_ORCAMENTO_PARA_120_FPS },
    { "Cidade/Orcamento para 240 fps", mc3_w_CIDADE_ORCAMENTO_PARA_240_FPS, 1, 0, MC3_EN_CIDADE_ORCAMENTO_PARA_240_FPS },
    { "Cidade/Orcamento para 1 fps", mc3_w_CIDADE_ORCAMENTO_PARA_1_FPS, 1, 0, MC3_EN_CIDADE_ORCAMENTO_PARA_1_FPS },
    { "Trafego/Distancia de desenho x2", mc3_w_TRAFEGO_DISTANCIA_DE_DESENHO_X2, 1, 0, MC3_EN_TRAFEGO_DISTANCIA_DE_DESENHO_X2 },
    { "Video/Widescreen 16:9", mc3_w_VIDEO_WIDESCREEN_16_9, 2, 0, MC3_EN_VIDEO_WIDESCREEN_16_9 },
    { "Video/Modo 640x448", mc3_w_VIDEO_MODO_640X448, 1, 1, MC3_EN_VIDEO_MODO_640X448 },
    { "Video/Modo 640x480 progressivo - EXPERIMENTAL", mc3_w_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL, 3, 0, MC3_EN_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL },
    { "Video/Render targets em 640 de largura", mc3_w_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA, 6, 0, MC3_EN_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA },
    { "Video/Desativar motion blur", mc3_w_VIDEO_DESATIVAR_MOTION_BLUR, 1, 0, MC3_EN_VIDEO_DESATIVAR_MOTION_BLUR },
};
static const int MC3_NGROUPS = 21;

// mutuamente exclusivos: escrevem o mesmo endereco
#if (MC3_EN_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS + MC3_EN_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS) > 1
#error "ligue so um: 60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS, 60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS"
#endif

// mutuamente exclusivos: escrevem o mesmo endereco
#if (MC3_EN_CIDADE_ORCAMENTO_PARA_120_FPS + MC3_EN_CIDADE_ORCAMENTO_PARA_1_FPS + MC3_EN_CIDADE_ORCAMENTO_PARA_240_FPS + MC3_EN_CIDADE_ORCAMENTO_PARA_60_FPS + MC3_EN_CIDADE_ORCAMENTO_PARA_90_FPS) > 1
#error "ligue so um: CIDADE_ORCAMENTO_PARA_120_FPS, CIDADE_ORCAMENTO_PARA_1_FPS, CIDADE_ORCAMENTO_PARA_240_FPS, CIDADE_ORCAMENTO_PARA_60_FPS, CIDADE_ORCAMENTO_PARA_90_FPS"
#endif

// mutuamente exclusivos: escrevem o mesmo endereco
#if (MC3_EN_VIDEO_MODO_640X448 + MC3_EN_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL) > 1
#error "ligue so um: VIDEO_MODO_640X448, VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL"
#endif

// mutuamente exclusivos: escrevem o mesmo endereco
#if (MC3_EN_VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL + MC3_EN_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA) > 1
#error "ligue so um: VIDEO_MODO_640X480_PROGRESSIVO_EXPERIMENTAL, VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA"
#endif

#endif
