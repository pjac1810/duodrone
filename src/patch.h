/* SPDX-License-Identifier: GPL-3.0-only */
/* DUODRONE: the sound's parameters, shared by the panel (main.c) and the DSP (dsp.c).
 *
 * The signal path follows a two-voice analogue drone synthesizer:
 *
 *   DRONE I  : X-WAVE osc + SUB (-1/-2) -> OSC MIX -> LP/HP resonant filter -> VOLUME (saturates) --+
 *   DRONE II : the same, can be hard-synced to / pitch-linked with DRONE I                          +-> + AUX IN
 *   ULTRA LFO: one RATE, two outputs (WAVE I, WAVE II), 13 shapes + OFF each, routed per voice       |
 *   MIXER    : -> dark delay (TIME, REPEATS, LFO) -> 8-algorithm reverb -> MAIN OUT ---------------+
 */
#ifndef DUODRONE_PATCH_H
#define DUODRONE_PATCH_H
#include <stdint.h>

enum {
  /* DRONE I, DRONE II: the same eight, in this order (P_XW + 8 * voice) */
  P_XW1, P_MIX1, P_OLFO1, P_PITCH1, P_CUT1, P_RES1, P_FLFO1, P_VOL1,
  P_XW2, P_MIX2, P_OLFO2, P_PITCH2, P_CUT2, P_RES2, P_FLFO2, P_VOL2,
  /* switches */
  P_SUB1, P_SUB2, P_ROUTE1, P_ROUTE2, P_SYNC, P_LINK, P_FT1, P_FT2,
  /* ULTRA LFO */
  P_LW1, P_LW2, P_RATE, P_OD1, P_OD2, P_FD1, P_FD2,
  /* delay */
  P_DMIX, P_DTIME, P_DREP, P_DLFO, P_DSRC,
  /* reverb, mixer */
  P_RMIX, P_RTYPE, P_AUX, P_MAIN,
  P_COUNT
};

#define VOICE_STRIDE 8 /* P_XW2 - P_XW1 */

/* pitch: cents above MIDI note 0 */
#define PITCH_MIN 1200  /* C0, 16.4 Hz */
#define PITCH_MAX 10800 /* C8, 4186 Hz */

/* LFO shapes (P_LW1 / P_LW2) */
enum {
  LFO_OFF, LFO_TRI, LFO_SQR, LFO_SAW, LFO_TRI90, LFO_SQR90, LFO_TRI2, LFO_SAW2,
  LFO_SQPAT2, LFO_SQPAT4, LFO_EXPSAW, LFO_EXPSAW2, LFO_EXPUD4, LFO_LOGTRI,
  LFO_SHAPES
};

#define REVERB_TYPES 8

typedef struct {
  int16_t p[P_COUNT];
} patch_t;

/* the live sound: written by the panel (one int16 at a time), read by audio() */
extern volatile int16_t PAR[P_COUNT];

/* dsp.c */
void dsp_init(void);
void dsp_render(int32_t *out, uint32_t frames, const int32_t *aux);
void dsp_lfo_reset(void);
void dsp_gate(int on);        /* the drones' amplifier: 1 sound, 0 fade out */
int32_t dsp_lfo_shape(int shape, uint32_t phase); /* one LFO shape at a phase, +-32767 (the panel draws them) */
extern volatile int32_t dsp_lfo_out[2]; /* WAVE I, WAVE II now, +-32767 (the panel's lamps) */
#define SCOPE_N 240
extern volatile int16_t dsp_scope[SCOPE_N]; /* the last ~240 output samples, decimated */
extern volatile uint32_t dsp_scope_seq;     /* bumped each time dsp_scope is refilled */
void dsp_scope_rearm(void);                 /* the panel has drawn it: capture the next */

#endif
