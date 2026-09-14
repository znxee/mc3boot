// -----------------------------------------------------------------------------
//  mc3mod - which patch groups the payload applies
//
//  This is the ONLY file you edit by hand. patches.h is generated from the real
//  .pnach files by:
//
//      python mc3_pnach.py --dir <cheats> --elf <elf> --to-cpp payload/patches.h
//
//  Every group defaults to 0 there, so anything not named here stays off, and
//  regenerating the table never touches your choices. Groups that write the same
//  address are mutually exclusive and turning two on is a compile error, not a
//  silent last-one-wins.
//
//  The defaults below reproduce the setup that is known to work with the PCSX2
//  NTSC frame rate set to 120 Hz.
// -----------------------------------------------------------------------------
#ifndef MC3_CONFIG_H
#define MC3_CONFIG_H

// --- frame rate -------------------------------------------------------------
// The vblank handler releases the game thread on every field. Two groups carry
// the same word; pick one.
#define MC3_EN_60_FPS_1_DESTRAVAR_PARA_60_FPS               1
#define MC3_EN_60_FPS                                       0

// --- loading screen ---------------------------------------------------------
// The constant has to match the real field rate. 2a = 60, 2b = 120.
#define MC3_EN_60_FPS_2A_FLASH_DA_TELA_DE_LOADING_60_FPS    0
#define MC3_EN_60_FPS_2B_FLASH_DA_TELA_DE_LOADING_120_FPS   1

// --- pedestrians ------------------------------------------------------------
// Two ways to do the same thing, and they must not both run:
//   the constant group halves the rate - correct only at exactly 60 fps
//   MC3_DYNAMIC_PEDS computes rate = base * dt * 30 every frame - any rate
// This is the one thing here a pnach genuinely cannot do, so it is the default.
#define MC3_EN_60_FPS_3_PEDESTRES_CONSTANTE_SO_PARA_60_FPS  0
#define MC3_DYNAMIC_PEDS                                    1

// --- city budget ------------------------------------------------------------
// The five constant groups patch the argument of a jal to
// mcCity::SetIntendedFrameRate. Two things follow from that:
//   - the value only reaches the city when the game next runs
//     mcGame::ApplyFrameRateConfig, which happens on transitions (garage, race
//     start), not every frame;
//   - it is a number you had to pick in advance, so it is wrong the moment the
//     frame rate is not the one you picked.
// MC3_DYNAMIC_CITY_RATE calls that setter itself, with the measured rate,
// whenever the rate actually moves. Off by default: it changes how the city
// budgets its work, so turn it on deliberately and not in the middle of another
// test.
#define MC3_DYNAMIC_CITY_RATE                               0
#define MC3_EN_CIDADE_ORCAMENTO_PARA_120_FPS                1
#define MC3_EN_TRAFEGO_DISTANCIA_DE_DESENHO_X2              0

// --- video / graphics -------------------------------------------------------
// 640X448 is patch=0 only: the payload starts inside mcGame, after video init
// has already read it, so from here it only takes effect on the next mode
// change. It is flagged `tarde` in the generated table. Keep it in the pnach if
// you need it at boot.
// proper_widescreen.mod owns the same projection words and can switch them at
// runtime. Keeping this legacy fixed profile enabled in peds.mod makes it win
// after the deferred module on every frame, so the HUD changes but 3D does not.
#define MC3_EN_VIDEO_WIDESCREEN_16_9                        0
#define MC3_EN_VIDEO_DESATIVAR_MOTION_BLUR                  0
#define MC3_EN_VIDEO_RENDER_TARGETS_EM_640_DE_LARGURA       0
#define MC3_EN_GRAPHICAL_REFLEXO_QUALIDADE_ALTA             0

// --- hardware ---------------------------------------------------------------
// Writing instruction words needs the caches sorted out before the CPU reaches
// them. PCSX2 notices the write on its own; a real R5900 does not. Costs one
// syscall on the frames where something actually changed, which is normally the
// first one only. No reason to turn it off except to isolate a hang.
#define MC3_FLUSH_CACHE                                     1

#endif
