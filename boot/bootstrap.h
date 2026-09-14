// GERADO por mc3_pnach.py --to-cpp -- nao edite a mao.
// Fonte: C:/Users/rodri/AppData/Local/Temp/claude/C--MC3test-Claudio/b71d00d7-f8af-4003-afc5-68c0f6d11632/scratchpad/cheats_ours  (crc 60A42FF5)
#ifndef MC3BOOT_PATCHES_H
#define MC3BOOT_PATCHES_H

#ifndef MC3_WRITE_TYPES
#define MC3_WRITE_TYPES
typedef struct { unsigned int addr; unsigned int value;
                 unsigned char size; } mc3_write;
typedef struct { const char* name; const mc3_write* w; unsigned short n;
                 unsigned char tarde; unsigned char enabled; } mc3_group;
#endif

#ifndef MC3BOOT_EN_HOSTFS_ARQUIVOS_SOLTOS_NECESSARIO
#define MC3BOOT_EN_HOSTFS_ARQUIVOS_SOLTOS_NECESSARIO 0
#endif
static const mc3_write mc3boot_w_HOSTFS_ARQUIVOS_SOLTOS_NECESSARIO[] = {
    { 0x001A0DC8u, 0x24A57DDDu, 4 },
    { 0x00398404u, 0x24020000u, 4 },
    { 0x00398580u, 0x24060005u, 4 },
    { 0x0039865Cu, 0x24060005u, 4 },
    { 0x0042BCFCu, 0x24030000u, 4 },
    { 0x00432314u, 0x00000000u, 4 },
    { 0x0043465Cu, 0x10620006u, 4 },
    { 0x004BAE10u, 0x24050350u, 4 },
    { 0x004BAE34u, 0x24050000u, 4 },
    { 0x004BB244u, 0x24060000u, 4 },
    { 0x00615AF4u, 0x00640D8Fu, 4 },
    { 0x00617F84u, 0x0065C51Cu, 4 },
    { 0x00618E64u, 0x3273702Eu, 4 },
    { 0x00618E68u, 0x79707374u, 4 },
    { 0x006192ACu, 0x3273702Eu, 4 },
    { 0x006192B0u, 0x79707374u, 4 },
    { 0x00637CE0u, 0x616E642Fu, 4 },
    { 0x00637CE4u, 0x65722E73u, 4 },
    { 0x00637CE8u, 0x0000006Cu, 4 },
    { 0x00637CECu, 0x00000000u, 4 },
    { 0x00637CF0u, 0x00000000u, 4 },
    { 0x00637CF4u, 0x00000000u, 4 },
    { 0x00637CF8u, 0x00000000u, 4 },
    { 0x00637CFCu, 0x00000000u, 4 },
    { 0x00637D00u, 0x00000000u, 4 },
    { 0x00637DCCu, 0x41007373u, 4 },
    { 0x00637DD0u, 0x54455353u, 4 },
    { 0x00637DD4u, 0x00000053u, 4 },
    { 0x00637DD8u, 0x00000000u, 4 },
    { 0x00640DECu, 0x5300313Bu, 4 },
    { 0x00640DF0u, 0x45545359u, 4 },
    { 0x00640DF4u, 0x00005C4Du, 4 },
    { 0x00640DF8u, 0x00000000u, 4 },
    { 0x00640DFCu, 0x00000000u, 4 },
    { 0x00640E00u, 0x00000000u, 4 },
    { 0x00640E04u, 0x00000000u, 4 },
    { 0x00640E08u, 0x00000000u, 4 },
    { 0x00659B0Cu, 0x5C3A3074u, 4 },
    { 0x00659B10u, 0x5547544Eu, 4 },
    { 0x00659B14u, 0x44564449u, 4 },
    { 0x00659B18u, 0x464C452Eu, 4 },
    { 0x00659B1Cu, 0x00000000u, 4 },
    { 0x00659B20u, 0x00000000u, 4 },
    { 0x0065C52Cu, 0x58453A74u, 4 },
    { 0x0065C530u, 0x633A4345u, 4 },
    { 0x0065C534u, 0x7261656Cu, 4 },
    { 0x0065C538u, 0x64616572u, 4 },
    { 0x0065C53Cu, 0x796C6E6Fu, 4 },
    { 0x0065C540u, 0x6578652Eu, 4 },
    { 0x0065C544u, 0x68000020u, 4 },
    { 0x0065C548u, 0x3A74736Fu, 4 },
    { 0x0065C54Cu, 0x43455845u, 4 },
    { 0x0065C550u, 0x6900003Au, 4 },
};

#ifndef MC3BOOT_EN_HOSTFS_CORRECAO_DE_STREAMS_DAT_MAIOR_QUE_2_GIB
#define MC3BOOT_EN_HOSTFS_CORRECAO_DE_STREAMS_DAT_MAIOR_QUE_2_GIB 0
#endif
static const mc3_write mc3boot_w_HOSTFS_CORRECAO_DE_STREAMS_DAT_MAIOR_QUE_2_GIB[] = {
    { 0x004F9D10u, 0x00052AC2u, 4 },
    { 0x004F9D34u, 0x00052AC2u, 4 },
    { 0x004F9D60u, 0x00052AC2u, 4 },
    { 0x004F9D8Cu, 0x00052AC2u, 4 },
    { 0x004FA340u, 0x00102AC2u, 4 },
};

static mc3_group mc3boot_groups[] = {
    { "HostFS/Arquivos soltos - NECESSARIO", mc3boot_w_HOSTFS_ARQUIVOS_SOLTOS_NECESSARIO, 53, 0, MC3BOOT_EN_HOSTFS_ARQUIVOS_SOLTOS_NECESSARIO },
    { "HostFS/Correcao de streams.dat maior que 2 GiB", mc3boot_w_HOSTFS_CORRECAO_DE_STREAMS_DAT_MAIOR_QUE_2_GIB, 5, 0, MC3BOOT_EN_HOSTFS_CORRECAO_DE_STREAMS_DAT_MAIOR_QUE_2_GIB },
};
static const int MC3BOOT_NGROUPS = 2;

#endif
