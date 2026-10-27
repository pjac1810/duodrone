/* SPDX-License-Identifier: GPL-3.0-only */
/* DUODRONE's eight factory sounds.
 *
 * Values: knobs 0..1023 (attenuverters -512..511, 0 = off), pitch in cents above MIDI note 0, switches 0/1,
 * LFO shapes 0..13 (0 = OFF), reverb types 0..7. X-WAVE: 0 triangle, 341 saw, 682 square, 1023 narrow pulse.
 * OSC MIX: 0 the X-WAVE alone, 512 both, 1023 the sub alone. CUTOFF: 0 = 20 Hz .. 1023 = 16 kHz (597 ~ 1 kHz).
 * VOLUME: 410 ~ unity, more saturates. LFO RATE: 0 = 0.02 Hz .. 1023 = 70 Hz (500 ~ 1 Hz). */
#include "presets.h"

#define N(midi) ((midi) * 100)

const preset_t FACTORY[FACTORY_COUNT] = {
    {"FIFTHS",
     {
         [P_XW1] = 341, [P_MIX1] = 300, [P_OLFO1] = 0, [P_PITCH1] = N(33), [P_CUT1] = 560, [P_RES1] = 300, [P_FLFO1] = 120, [P_VOL1] = 420,
         [P_XW2] = 341, [P_MIX2] = 300, [P_OLFO2] = 0, [P_PITCH2] = N(40), [P_CUT2] = 540, [P_RES2] = 300, [P_FLFO2] = -100, [P_VOL2] = 400,
         [P_SUB1] = 0, [P_SUB2] = 0, [P_ROUTE1] = 0, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_TRI, [P_LW2] = LFO_TRI90, [P_RATE] = 250, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 200, [P_DTIME] = 520, [P_DREP] = 400, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 380, [P_RTYPE] = 1, [P_AUX] = 512, [P_MAIN] = 600,
     }},
    {"DEEP SPACE",
     {
         [P_XW1] = 120, [P_MIX1] = 600, [P_OLFO1] = 0, [P_PITCH1] = N(36), [P_CUT1] = 380, [P_RES1] = 500, [P_FLFO1] = 200, [P_VOL1] = 520,
         [P_XW2] = 160, [P_MIX2] = 560, [P_OLFO2] = 0, [P_PITCH2] = N(36) + 7, [P_CUT2] = 400, [P_RES2] = 480, [P_FLFO2] = 180, [P_VOL2] = 500,
         [P_SUB1] = 1, [P_SUB2] = 0, [P_ROUTE1] = 0, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_EXPSAW, [P_LW2] = LFO_LOGTRI, [P_RATE] = 120, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 300, [P_DTIME] = 800, [P_DREP] = 550, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 600, [P_RTYPE] = 5, [P_AUX] = 512, [P_MAIN] = 680,
     }},
    {"PWM CHOIR",
     {
         [P_XW1] = 850, [P_MIX1] = 200, [P_OLFO1] = 160, [P_PITCH1] = N(38), [P_CUT1] = 650, [P_RES1] = 150, [P_FLFO1] = 0, [P_VOL1] = 400,
         [P_XW2] = 850, [P_MIX2] = 200, [P_OLFO2] = -160, [P_PITCH2] = N(45), [P_CUT2] = 640, [P_RES2] = 150, [P_FLFO2] = 0, [P_VOL2] = 400,
         [P_SUB1] = 0, [P_SUB2] = 0, [P_ROUTE1] = 0, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_TRI, [P_LW2] = LFO_TRI90, [P_RATE] = 350, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 120, [P_DTIME] = 300, [P_DREP] = 250, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 520, [P_RTYPE] = 0, [P_AUX] = 512, [P_MAIN] = 600,
     }},
    {"SYNC SCREAM",
     {
         [P_XW1] = 341, [P_MIX1] = 0, [P_OLFO1] = 0, [P_PITCH1] = N(33), [P_CUT1] = 700, [P_RES1] = 200, [P_FLFO1] = 0, [P_VOL1] = 400,
         [P_XW2] = 341, [P_MIX2] = 150, [P_OLFO2] = 300, [P_PITCH2] = N(52), [P_CUT2] = 720, [P_RES2] = 650, [P_FLFO2] = 150, [P_VOL2] = 650,
         [P_SUB1] = 0, [P_SUB2] = 0, [P_ROUTE1] = 0, [P_ROUTE2] = 1, [P_SYNC] = 1, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_SAW, [P_LW2] = LFO_TRI, [P_RATE] = 420, [P_OD1] = 0, [P_OD2] = 0, [P_FD1] = 1, [P_FD2] = 1,
         [P_DMIX] = 260, [P_DTIME] = 400, [P_DREP] = 500, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 300, [P_RTYPE] = 3, [P_AUX] = 512, [P_MAIN] = 680,
     }},
    {"CATHEDRAL",
     {
         [P_XW1] = 0, [P_MIX1] = 400, [P_OLFO1] = 200, [P_PITCH1] = N(36), [P_CUT1] = 800, [P_RES1] = 100, [P_FLFO1] = 0, [P_VOL1] = 420,
         [P_XW2] = 0, [P_MIX2] = 400, [P_OLFO2] = -200, [P_PITCH2] = N(43), [P_CUT2] = 800, [P_RES2] = 100, [P_FLFO2] = 0, [P_VOL2] = 420,
         [P_SUB1] = 0, [P_SUB2] = 0, [P_ROUTE1] = 0, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_TRI, [P_LW2] = LFO_LOGTRI, [P_RATE] = 80, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 0, [P_DTIME] = 512, [P_DREP] = 300, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 700, [P_RTYPE] = 5, [P_AUX] = 512, [P_MAIN] = 520,
     }},
    {"PULSE TRAIN",
     {
         [P_XW1] = 682, [P_MIX1] = 250, [P_OLFO1] = 0, [P_PITCH1] = N(28), [P_CUT1] = 380, [P_RES1] = 600, [P_FLFO1] = 380, [P_VOL1] = 450,
         [P_XW2] = 682, [P_MIX2] = 250, [P_OLFO2] = 0, [P_PITCH2] = N(40), [P_CUT2] = 400, [P_RES2] = 580, [P_FLFO2] = 300, [P_VOL2] = 430,
         [P_SUB1] = 0, [P_SUB2] = 1, [P_ROUTE1] = 0, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_SQPAT4, [P_LW2] = LFO_SQPAT2, [P_RATE] = 520, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 380, [P_DTIME] = 600, [P_DREP] = 600, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 250, [P_RTYPE] = 4, [P_AUX] = 512, [P_MAIN] = 720,
     }},
    {"SPRING TWANG",
     {
         [P_XW1] = 500, [P_MIX1] = 100, [P_OLFO1] = 60, [P_PITCH1] = N(43), [P_CUT1] = 420, [P_RES1] = 550, [P_FLFO1] = 0, [P_VOL1] = 560,
         [P_XW2] = 500, [P_MIX2] = 100, [P_OLFO2] = 0, [P_PITCH2] = N(50), [P_CUT2] = 440, [P_RES2] = 520, [P_FLFO2] = 120, [P_VOL2] = 540,
         [P_SUB1] = 0, [P_SUB2] = 0, [P_ROUTE1] = 1, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 1, [P_FT2] = 1,
         [P_LW1] = LFO_EXPUD4, [P_LW2] = LFO_TRI, [P_RATE] = 460, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 150, [P_DTIME] = 350, [P_DREP] = 300, [P_DLFO] = 0, [P_DSRC] = 0,
         [P_RMIX] = 550, [P_RTYPE] = 6, [P_AUX] = 512, [P_MAIN] = 700,
     }},
    {"INFINITE",
     {
         [P_XW1] = 200, [P_MIX1] = 700, [P_OLFO1] = 0, [P_PITCH1] = N(29), [P_CUT1] = 500, [P_RES1] = 350, [P_FLFO1] = 250, [P_VOL1] = 420,
         [P_XW2] = 300, [P_MIX2] = 600, [P_OLFO2] = 220, [P_PITCH2] = N(36) + 5, [P_CUT2] = 520, [P_RES2] = 300, [P_FLFO2] = 0, [P_VOL2] = 400,
         [P_SUB1] = 1, [P_SUB2] = 1, [P_ROUTE1] = 0, [P_ROUTE2] = 0, [P_SYNC] = 0, [P_LINK] = 1, [P_FT1] = 0, [P_FT2] = 0,
         [P_LW1] = LFO_LOGTRI, [P_LW2] = LFO_EXPSAW2, [P_RATE] = 60, [P_OD1] = 0, [P_OD2] = 1, [P_FD1] = 0, [P_FD2] = 1,
         [P_DMIX] = 300, [P_DTIME] = 900, [P_DREP] = 700, [P_DLFO] = 200, [P_DSRC] = 1,
         [P_RMIX] = 650, [P_RTYPE] = 7, [P_AUX] = 512, [P_MAIN] = 600,
     }},
};
