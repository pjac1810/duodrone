/* SPDX-License-Identifier: GPL-3.0-only */
/* DUODRONE's sound: two drone voices, the ULTRA LFO, a dark delay and an 8-algorithm reverb.
 * Integers only (the FM-1 has no FPU): audio is Q15 (+-32767 = full scale), phases are 32-bit.
 *
 * Per voice: an anti-aliased (polyBLEP) X-WAVE oscillator that morphs TRIANGLE -> SAW -> SQUARE -> narrow PULSE,
 * a square sub-oscillator one or two octaves down, OSC MIX between them, a 4-pole OTA-style ladder (24 dB LP, or
 * HP by pole mixing) with resonance up to self-oscillation and a saturating input, and a VOLUME stage that clips
 * softly when pushed (past ~3 o'clock, as on the analogue original).
 *
 * The modulation (LFO, pitch, cutoff) is computed every CTRL frames (~2.8 kHz); the oscillators and filters run at
 * 44.1 kHz; the delay and the reverb run at half rate (22.05 kHz: darker, as their vintage originals, and half the
 * cost), interpolated back up. */
#include "fm1.h"
#include "patch.h"
#include "tables.h"

#define CTRL 16u
#define Q15 32767

volatile int16_t PAR[P_COUNT];
volatile int32_t dsp_lfo_out[2];
volatile int16_t dsp_scope[SCOPE_N];
volatile uint32_t dsp_scope_seq;

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* soft clip: linear around 0, a cubic knee, flat at +-1.5 in -> +-1 out (Q15 in and out) */
static inline int32_t sat15(int32_t x) {
  if (x >= 49152) return Q15;
  if (x <= -49152) return -Q15;
  int32_t x2 = (int32_t)(((int64_t)x * x) >> 15);
  int32_t x3 = (int32_t)(((int64_t)x2 * x) >> 15);
  return x - ((x3 * 4855) >> 15); /* x - 4/27 x^3 */
}

/* phase step per frame for a pitch in cents above MIDI note 0; -2400 .. 13199 */
static uint32_t pitch_inc(int32_t cents) {
  int32_t c = clampi(cents, -2400, 13199) + 2400;
  int32_t oct = c / 1200, rem = c - oct * 1200;
  return PITCH_TAB[rem] >> (12 - oct);
}

/* ---------------------------------------------------------------- oscillators */

typedef struct {
  uint32_t ph, inc, rcp; /* rcp: 2^32 / (inc >> 16), per control block, for the BLEPs */
  uint8_t cyc;           /* cycles counted: the sub-oscillator's divider */
} osc_t;

/* polyBLEP for a unit step at phase 0, Q15 (-32768 .. 32768) */
static inline int32_t blep(uint32_t t, uint32_t inc, uint32_t rcp) {
  if (t < inc) {
    int32_t x = (int32_t)(((uint64_t)(t >> 16) * rcp) >> 17);
    return 2 * x - ((x * x) >> 15) - 32768;
  }
  uint32_t d = 0u - t;
  if (d < inc) {
    int32_t x = 32768 - (int32_t)(((uint64_t)(d >> 16) * rcp) >> 17);
    return (x * x) >> 15;
  }
  return 0;
}

/* one sample of the X-WAVE + sub mix. seg 0: tri->saw, 1: saw->square, 2: square->narrow pulse; f: Q15 within the
 * segment; pw: the pulse's width (phase). submask 1: -1 octave, 2: -2. gx, gs: OSC MIX gains, Q10 */
static inline int32_t osc_sample(osc_t *o, int seg, int32_t f, uint32_t pw, uint8_t submask, int32_t gx, int32_t gs) {
  uint32_t ph = o->ph, inc = o->inc, rcp = o->rcp;
  int32_t saw = 0, sq = 0, x;
  int32_t b0 = blep(ph, inc, rcp);
  if (seg == 0) {
    int32_t s = (int32_t)(ph >> 16) - 32768;
    int32_t tri = 2 * (s < 0 ? -s : s) - 32768;
    saw = s - b0;
    x = tri + (int32_t)(((int64_t)(saw - tri) * f) >> 15);
  } else if (seg == 1) {
    saw = (int32_t)(ph >> 16) - 32768 - b0;
    sq = (ph < 2147483648u ? Q15 : -Q15) + b0 - blep(ph - 2147483648u, inc, rcp);
    x = saw + (int32_t)(((int64_t)(sq - saw) * f) >> 15);
  } else {
    x = (ph < pw ? Q15 : -Q15) + b0 - blep(ph - pw, inc, rcp);
    x -= (int32_t)(pw >> 16) - 32768; /* a narrow pulse sits off centre: take its DC out */
  }
  /* the sub: a square that flips on the main oscillator's wraps (they carry its BLEP too) */
  int32_t sub = (o->cyc & submask) ? Q15 : -Q15;
  if (b0) {
    uint8_t before = o->cyc, after = (uint8_t)(o->cyc + 1);
    if (ph < inc) after = o->cyc, before = (uint8_t)(o->cyc - 1);
    int sb = (before & submask) != 0, sa = (after & submask) != 0;
    if (sa != sb) sub += sa ? b0 : -b0;
  }
  return (x * gx + sub * gs) >> 11;
}

/* ---------------------------------------------------------------- the 4-pole filter */

typedef struct {
  int32_t s1, s2, s3, s4, y4; /* Q23 (Q15 << 8) */
  int32_t dc;                 /* the output's coupling capacitor (a ~8 Hz high-pass), Q8 */
} filt_t;

/* the per-block coefficients: G (Q16) and its powers, k (Q12, 4.0 = self-oscillation), 1 / (1 + k G^4) (Q16) */
typedef struct {
  int32_t G, G2, G3, G4, k, inv;
} fcoef_t;

static void filt_coef(fcoef_t *c, int32_t G, int32_t k) {
  c->G = G;
  c->G2 = (int32_t)(((int64_t)G * G) >> 16);
  c->G3 = (int32_t)(((int64_t)c->G2 * G) >> 16);
  c->G4 = (int32_t)(((int64_t)c->G3 * G) >> 16);
  c->k = k;
  c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + (int32_t)(((int64_t)k * c->G4) >> 12)));
}

/* a zero-delay-feedback ladder: the loop is solved each sample (linear), then run with the saturating input, so the
 * resonance tracks the cutoff everywhere and self-oscillates at k = 4 */
static inline int32_t filt_sample(filt_t *f, int32_t in, const fcoef_t *c, int hp) {
  int32_t X = in * 256;
  X += (int32_t)(((int64_t)X * c->k) >> 14); /* the passband's level, kept as resonance rises */
  int32_t G = c->G, iG = 65536 - G;
  int64_t S1 = ((int64_t)f->s1 * iG) >> 16, S2 = ((int64_t)f->s2 * iG) >> 16, S3 = ((int64_t)f->s3 * iG) >> 16,
          S4 = ((int64_t)f->s4 * iG) >> 16;
  int64_t sig = ((S1 * c->G3) >> 16) + ((S2 * c->G2) >> 16) + ((S3 * G) >> 16) + S4;
  int64_t y4e = ((((int64_t)X * c->G4) >> 16) + sig) * c->inv >> 16;
  int32_t u = X - (int32_t)((c->k * y4e) >> 12);
  u = sat15(u >> 9) * 512; /* the input stage saturates (at +-2) */
  int32_t v, y1, y2, y3, y4;
  v = (int32_t)(((int64_t)(u - f->s1) * G) >> 16), y1 = v + f->s1, f->s1 = y1 + v;
  v = (int32_t)(((int64_t)(y1 - f->s2) * G) >> 16), y2 = v + f->s2, f->s2 = y2 + v;
  v = (int32_t)(((int64_t)(y2 - f->s3) * G) >> 16), y3 = v + f->s3, f->s3 = y3 + v;
  v = (int32_t)(((int64_t)(y3 - f->s4) * G) >> 16), y4 = v + f->s4, f->s4 = y4 + v;
  f->y4 = y4;
  int32_t y = hp ? (u - 4 * y1 + 6 * y2 - 4 * y3 + y4) : y4; /* Q23 */
  f->dc += (y - f->dc) >> 10;                                /* AC coupled, as the analogue output is */
  return (y - f->dc) >> 8;
}

/* ---------------------------------------------------------------- the ULTRA LFO */

static uint32_t lfo_ph;
static volatile uint8_t lfo_reset_req;

static inline int32_t w_saw(uint32_t p) { return (int32_t)(p >> 16) - 32768; }
static inline int32_t w_tri(uint32_t p) {
  int32_t s = w_saw(p);
  return 2 * (s < 0 ? -s : s) - 32768;
}
static inline int32_t w_sqr(uint32_t p) { return p < 2147483648u ? Q15 : -Q15; }
static inline int32_t w_exp(uint32_t p) { /* an exponential-looking rise, -1 .. 1 */
  int32_t u = (int32_t)(p >> 17);           /* 0..32767 */
  int32_t e = (int32_t)(((int64_t)((u * u) >> 15) * u) >> 15);
  return 2 * e - Q15;
}

int32_t dsp_lfo_shape(int shape, uint32_t p) {
  switch (shape) {
    case LFO_TRI: return w_tri(p);
    case LFO_SQR: return w_sqr(p);
    case LFO_SAW: return w_saw(p);
    case LFO_TRI90: return w_tri(p + 1073741824u);
    case LFO_SQR90: return w_sqr(p + 1073741824u);
    case LFO_TRI2: return w_tri(p << 1);
    case LFO_SAW2: return w_saw(p << 1);
    case LFO_SQPAT2: { /* a square at twice the rate, full then half height */
      int32_t lv = p < 2147483648u ? Q15 : 16384;
      return w_sqr(p << 1) > 0 ? lv : -lv;
    }
    case LFO_SQPAT4: { /* a square at four times the rate, stepping down 4/4, 3/4, 2/4, 1/4 */
      static const int32_t LV[4] = {32767, 24576, 16384, 8192};
      int32_t lv = LV[p >> 30];
      return w_sqr(p << 2) > 0 ? lv : -lv;
    }
    case LFO_EXPSAW: return w_exp(p);
    case LFO_EXPSAW2: return w_exp(p << 1);
    case LFO_EXPUD4: { /* four exponential ramps a cycle, up, down, up, down */
      int32_t e = w_exp(p << 2);
      return (p >> 30) & 1u ? -e : e;
    }
    case LFO_LOGTRI: { /* a triangle with logarithmic sides: quick to rise, slow to settle */
      int32_t t = (w_tri(p) + 32768) >> 1; /* 0..32767 */
      int32_t m = Q15 - t;
      int32_t c = (int32_t)(((int64_t)((m * m) >> 15) * m) >> 15);
      return 2 * (Q15 - c) - Q15;
    }
    default: return 0;
  }
}

/* ---------------------------------------------------------------- the delay (half rate) */

#define DL_N 22050 /* 1 s at 22.05 kHz */
static int16_t dl_buf[DL_N] POOL;
static int32_t dl_w, dl_lenq8 = 6000 << 8, dl_lp, dl_hp;

/* ---------------------------------------------------------------- the reverb (half rate): an FDN of four lines */

#define RV_PRE_N 2208
static const int16_t RV_LEN[4] = {2203, 2617, 3011, 3469};
static const int16_t AP_LEN[4] = {142, 107, 379, 277};
#define RV_MOD 24 /* room for the modulated reads */
static int16_t rv_pre[RV_PRE_N] POOL;
static int16_t rv_l0[2203 + RV_MOD] POOL, rv_l1[2617 + RV_MOD] POOL, rv_l2[3011 + RV_MOD] POOL, rv_l3[3469 + RV_MOD] POOL;
static int16_t rv_a0[142] POOL, rv_a1[107] POOL, rv_a2[379] POOL, rv_a3[277] POOL;
static int16_t *const RV_L[4] = {rv_l0, rv_l1, rv_l2, rv_l3};
static int16_t *const RV_A[4] = {rv_a0, rv_a1, rv_a2, rv_a3};
static int32_t rv_w[4], rv_aw[4], rv_pw, rv_lp[4];
static uint32_t rv_mph;
static uint8_t rv_type = 255;
static volatile uint8_t rv_clear;

typedef struct {
  int16_t size;   /* Q10 of the lines' lengths */
  int16_t decay;  /* Q15 feedback */
  int16_t damp;   /* Q15 one-pole coefficient in the loop: low = dark */
  int16_t pre;    /* pre-delay, half-rate frames */
  int16_t diff;   /* Q15 input allpass gain */
  int16_t mod;    /* modulation depth, Q8 frames */
  int16_t modhz;  /* modulation phase step / 65536 */
  int16_t gain;   /* Q10 output */
} rv_algo_t;

/* CHORUS/HALL, HALL 1, HALL 2, PLATE, ROOM, CATHEDRAL, SPRING, INFINITE */
static const rv_algo_t RV_ALGO[REVERB_TYPES] = {
    {760, 27800, 15000, 300, 20000, 18 * 256, 160, 1100},  /* CH/HALL: modulated deeply, a chorus on the tail */
    {720, 28600, 13000, 400, 20500, 3 * 256, 70, 1000},    /* HALL 1 */
    {1000, 30400, 17000, 700, 21000, 4 * 256, 60, 900},    /* HALL 2: bigger, brighter */
    {430, 29600, 24000, 40, 24500, 2 * 256, 90, 1150},     /* PLATE: dense and bright */
    {260, 24000, 15000, 120, 18000, 1 * 256, 80, 1300},    /* ROOM */
    {1024, 31600, 11000, 1600, 22000, 5 * 256, 40, 640},   /* CATHEDRAL */
    {110, 30200, 21000, 0, 26000, 8 * 256, 230, 1250},     /* SPRING: short, ringing, wobbly */
    {900, 32600, 14000, 500, 21500, 6 * 256, 50, 800},     /* INFINITE: almost no decay */
};

static inline int16_t s16(int32_t v) { return (int16_t)clampi(v, -32767, 32767); }

/* a fractional read Q8 frames behind write position w in a ring of n */
static inline int32_t ring_read(const int16_t *b, int32_t n, int32_t w, int32_t dq8) {
  int32_t i = w - (dq8 >> 8), f = dq8 & 255;
  if (i < 0) i += n;
  int32_t j = i - 1;
  if (j < 0) j += n;
  return (b[i] * (256 - f) + b[j] * f) >> 8;
}

static void reverb_half(int32_t in, int32_t *ol, int32_t *or_) {
  int t = PAR[P_RTYPE];
  if (t < 0 || t >= REVERB_TYPES) t = 0;
  if (rv_clear || t != rv_type) { /* a new space starts empty: no burst from the old lines */
    for (int l = 0; l < 4; l++) {
      for (int i = 0; i < RV_LEN[l] + RV_MOD; i++) RV_L[l][i] = 0;
      for (int i = 0; i < AP_LEN[l]; i++) RV_A[l][i] = 0;
      rv_lp[l] = 0;
    }
    for (int i = 0; i < RV_PRE_N; i++) rv_pre[i] = 0;
    rv_type = (uint8_t)t, rv_clear = 0;
  }
  const rv_algo_t *a = &RV_ALGO[t];
  /* pre-delay */
  rv_pre[rv_pw] = s16(in);
  int32_t pi = rv_pw - a->pre;
  if (pi < 0) pi += RV_PRE_N;
  int32_t x = rv_pre[pi];
  if (++rv_pw >= RV_PRE_N) rv_pw = 0;
  /* input diffusion: four allpasses */
  for (int k = 0; k < 4; k++) {
    int16_t *b = RV_A[k];
    int32_t v = b[rv_aw[k]];
    int32_t y = v - ((a->diff * x) >> 15);
    b[rv_aw[k]] = s16(x + ((a->diff * y) >> 15));
    if (++rv_aw[k] >= AP_LEN[k]) rv_aw[k] = 0;
    x = y;
  }
  /* the four lines: read (two of them modulated), damp, mix (Hadamard), feed back */
  rv_mph += (uint32_t)a->modhz << 16;
  int32_t m0 = (sine(rv_mph) * a->mod) >> 15, m1 = (sine(rv_mph + 1431655765u) * a->mod) >> 15;
  int32_t r[4];
  for (int l = 0; l < 4; l++) {
    int32_t n = RV_LEN[l] + RV_MOD;
    int32_t len = (RV_LEN[l] * a->size) >> 10;
    if (len < 40) len = 40;
    int32_t dq8 = (len << 8) + (l == 0 ? m0 : l == 2 ? m1 : 0) + (RV_MOD / 2 << 8);
    int32_t v = ring_read(RV_L[l], n, rv_w[l], dq8);
    rv_lp[l] += ((v - rv_lp[l]) * a->damp) >> 15;
    r[l] = rv_lp[l];
  }
  int32_t h0 = (r[0] + r[1] + r[2] + r[3]) >> 1, h1 = (r[0] - r[1] + r[2] - r[3]) >> 1;
  int32_t h2 = (r[0] + r[1] - r[2] - r[3]) >> 1, h3 = (r[0] - r[1] - r[2] + r[3]) >> 1;
  int32_t h[4] = {h0, h1, h2, h3};
  for (int l = 0; l < 4; l++) {
    int32_t n = RV_LEN[l] + RV_MOD;
    int32_t fb = (h[l] * a->decay) >> 15;
    RV_L[l][rv_w[l]] = s16(sat15(fb + x));
    if (++rv_w[l] >= n) rv_w[l] = 0;
  }
  *ol = ((r[0] + r[2] - (r[1] >> 1)) * a->gain) >> 10;
  *or_ = ((r[1] + r[3] - (r[2] >> 1)) * a->gain) >> 10;
}

/* ---------------------------------------------------------------- the voices' state */

static osc_t osc[2];
static filt_t flt[2];
static int32_t cut_s[2] = {700 << 6, 700 << 6}; /* smoothed cutoff index, Q6 */
static int32_t pitch_s[2] = {4500 << 4, 4500 << 4}; /* smoothed pitch, cents Q4 (a short glide) */
static int32_t vol_g[2];                             /* the volume's gain now, Q10 */
static int32_t gate_g, gate_t = Q15;                 /* the drones' amplifier, Q15 */

/* the effects at half rate: the last two outputs, to interpolate between */
static int32_t out_dcl, out_dcr; /* the main outputs' coupling (~5 Hz high-pass), Q8 */
static int32_t fx_half, fx_acc, dly_prev, dly_cur, rvl_prev, rvl_cur, rvr_prev, rvr_cur;

/* the scope: a capture of decimated output, started on a rising zero crossing */
static uint8_t sc_state, sc_dec = 4, sc_cnt;
static int16_t sc_pos, sc_wait;
static int32_t sc_prev;

void dsp_init(void) {
  for (int i = 0; i < DL_N; i++) dl_buf[i] = 0;
  rv_clear = 1;
  gate_g = 0;
}

void dsp_lfo_reset(void) { lfo_reset_req = 1; }
void dsp_gate(int on) { gate_t = on ? Q15 : 0; }

void dsp_render(int32_t *out, uint32_t frames, const int32_t *aux) {
  for (uint32_t b0 = 0; b0 < frames; b0 += CTRL) {
    uint32_t n = frames - b0 < CTRL ? frames - b0 : CTRL;

    /* ---- control rate: the LFO, the routings, pitches and cutoffs */
    if (lfo_reset_req) lfo_ph = 0, lfo_reset_req = 0;
    int32_t rate = clampi(PAR[P_RATE], 0, 1023);
    uint32_t linc = pitch_inc(-2010 + ((rate * 14146) >> 10)) >> 7; /* 0.02 .. 70 Hz */
    int32_t lw[2];
    for (int w = 0; w < 2; w++) lw[w] = dsp_lfo_shape(PAR[P_LW1 + w], lfo_ph);
    lfo_ph += linc * n;
    dsp_lfo_out[0] = lw[0], dsp_lfo_out[1] = lw[1];

    int seg[2], hp[2];
    int32_t fq[2], gx[2], gs[2], vg_to[2];
    fcoef_t fc[2];
    uint32_t pw[2];
    uint8_t subm[2];
    for (int v = 0; v < 2; v++) {
      int o = v * VOICE_STRIDE;
      int32_t lo = lw[PAR[v ? P_OD2 : P_OD1] ? 1 : 0], lf = lw[PAR[v ? P_FD2 : P_FD1] ? 1 : 0];
      int32_t olfo = PAR[P_OLFO1 + o], xw = PAR[P_XW1 + o];
      int32_t cents = PAR[P_PITCH1 + o];
      /* the pitch glides a little, as a knob turned on the panel does */
      pitch_s[v] += ((cents << 4) - pitch_s[v]) >> 5;
      cents = pitch_s[v] >> 4;
      if (PAR[v ? P_ROUTE2 : P_ROUTE1]) { /* LFO -> PITCH, a squared taper */
        int32_t amt = (olfo * (olfo < 0 ? -olfo : olfo)) >> 9;
        cents += (lo * amt) >> 13;
      } else { /* LFO -> X-WAVE */
        xw += (lo * olfo) >> 14;
      }
      xw = clampi(xw, 0, 1023);
      uint32_t inc = pitch_inc(clampi(cents, 600, 12700));
      osc[v].inc = inc;
      osc[v].rcp = 0xFFFFFFFFu / (inc >> 16);
      if (xw < 341) seg[v] = 0, fq[v] = xw * 96;
      else if (xw < 682) seg[v] = 1, fq[v] = (xw - 341) * 96;
      else seg[v] = 2, fq[v] = (xw - 682) * 96;
      pw[v] = 2147483648u - (uint32_t)clampi(fq[v], 0, 32767) * 57600u; /* 50 % -> ~6 % */
      subm[v] = PAR[v ? P_SUB2 : P_SUB1] ? 2 : 1;
      int32_t m = clampi(PAR[P_MIX1 + o], 0, 1023);
      gx[v] = clampi(2 * (1023 - m), 0, 1023), gs[v] = clampi(2 * m, 0, 1023);
      int32_t cut = PAR[P_CUT1 + o] + ((lf * PAR[P_FLFO1 + o]) >> 14);
      cut_s[v] += ((clampi(cut, 0, 1023) << 6) - cut_s[v]) >> 1;
      int32_t res = clampi(PAR[P_RES1 + o], 0, 1023);
      filt_coef(&fc[v], FILT_G[clampi(cut_s[v] >> 6, 0, 1023)], res * 17); /* k: 0 .. ~4.25 */
      hp[v] = PAR[v ? P_FT2 : P_FT1] != 0;
      vg_to[v] = (clampi(PAR[P_VOL1 + o], 0, 1023) * 5) >> 1; /* 0 .. 2.5 */
    }
    int sync = PAR[P_SYNC] != 0;
    int32_t auxg = clampi(PAR[P_AUX], 0, 1023);

    /* delay: TIME, REPEATS, MIX and its LFO */
    int32_t dt = clampi(PAR[P_DTIME], 0, 1023);
    int32_t dlen = 660 + (int32_t)(((int64_t)20340 * dt * dt) >> 20); /* 30 ms .. ~950 ms, squared taper */
    int32_t dlfo = (lw[PAR[P_DSRC] ? 1 : 0] * PAR[P_DLFO]) >> 15;       /* +-511 */
    int32_t dlen_to = (dlen << 8) + dlfo * 90;                            /* +- ~8 ms */
    int32_t drep = (clampi(PAR[P_DREP], 0, 1023) * 35400) >> 10;          /* to ~1.08: it can run away, softly */
    int32_t dmix = clampi(PAR[P_DMIX], 0, 1023), rmix = clampi(PAR[P_RMIX], 0, 1023);
    int32_t d_dry = 1024 - (dmix >> 1), d_wet = dmix, r_dry = 1024 - (rmix >> 1), r_wet = rmix; /* Q10 */
    int32_t mg = clampi(PAR[P_MAIN], 0, 1023) * 2; /* Q10, 0 .. 2 */

    /* ---- audio rate */
    for (uint32_t i = 0; i < n; i++) {
      int32_t g = gate_g;
      gate_g += gate_g < gate_t ? 15 : gate_g > gate_t ? -15 : 0; /* ~50 ms fades */
      if ((gate_g > gate_t && gate_g - gate_t < 15) || (gate_g < gate_t && gate_t - gate_g < 15)) gate_g = gate_t;

      int32_t dry = 0;
      for (int v = 0; v < 2; v++) {
        int32_t s = osc_sample(&osc[v], seg[v], fq[v], pw[v], subm[v], gx[v], gs[v]);
        s = filt_sample(&flt[v], s, &fc[v], hp[v]);
        vol_g[v] += (vg_to[v] - vol_g[v]) >> 6;
        s = sat15((s * vol_g[v]) >> 10);
        if (!(v == 0 && sync)) dry += s; /* hard sync: DRONE I only drives DRONE II, it is muted in the mix */
      }
      /* advance the oscillators; DRONE II restarts when DRONE I wraps (hard sync) */
      uint32_t p0 = osc[0].ph + osc[0].inc;
      int wrap0 = p0 < osc[0].ph;
      osc[0].ph = p0;
      if (wrap0) osc[0].cyc++;
      uint32_t p1 = osc[1].ph + osc[1].inc;
      if (p1 < osc[1].ph) osc[1].cyc++;
      if (sync && wrap0) { /* where in the frame DRONE I wrapped, DRONE II starts again */
        uint32_t frac = (uint32_t)(((uint64_t)(p0 >> 16) * osc[0].rcp) >> 16); /* Q16 */
        p1 = (uint32_t)(((uint64_t)osc[1].inc * clampi((int32_t)frac, 0, 65535)) >> 16);
        osc[1].cyc++;
      }
      osc[1].ph = p1;

      dry = (int32_t)(((int64_t)dry * g) >> 15);
      if (aux) dry += (((aux[2 * (b0 + i)] + aux[2 * (b0 + i) + 1]) >> 1) * auxg) >> 10;

      /* ---- half rate: delay, then reverb */
      fx_acc += dry;
      if (fx_half) {
        int32_t xin = fx_acc >> 1;
        fx_acc = 0;
        dl_lenq8 += (dlen_to - dl_lenq8) >> 9; /* the time glides: turning TIME bends the repeats' pitch */
        int32_t lq = clampi(dl_lenq8, 2 << 8, (DL_N - 4) << 8);
        int32_t rd = ring_read(dl_buf, DL_N, dl_w, lq) * 2;
        dl_lp += ((rd - dl_lp) * 15000) >> 15;  /* dark: ~2.4 kHz in the loop */
        dl_hp += ((dl_lp - dl_hp) * 560) >> 15; /* and no rumble */
        int32_t wet = dl_lp - dl_hp;
        int32_t w = (xin >> 1) + (int32_t)(((int64_t)wet * drep) >> 16);
        dl_buf[dl_w] = s16(sat15(w));
        if (++dl_w >= DL_N) dl_w = 0;
        dly_prev = dly_cur, dly_cur = wet;
        int32_t dmixed = ((xin * d_dry) >> 10) + ((wet * d_wet) >> 10);
        int32_t l, r;
        reverb_half(dmixed >> 2, &l, &r);
        rvl_prev = rvl_cur, rvl_cur = l * 4;
        rvr_prev = rvr_cur, rvr_cur = r * 4;
      }
      int32_t dw = fx_half ? dly_cur : (dly_prev + dly_cur) >> 1;
      int32_t rl = fx_half ? rvl_cur : (rvl_prev + rvl_cur) >> 1;
      int32_t rr = fx_half ? rvr_cur : (rvr_prev + rvr_cur) >> 1;
      fx_half ^= 1;

      int32_t m = ((dry * d_dry) >> 10) + ((dw * d_wet) >> 10);
      int32_t L = ((m * r_dry) >> 10) + ((rl * r_wet) >> 10);
      int32_t R = ((m * r_dry) >> 10) + ((rr * r_wet) >> 10);
      out_dcl += ((L * 256) - out_dcl) >> 11, out_dcr += ((R * 256) - out_dcr) >> 11;
      L -= out_dcl >> 8, R -= out_dcr >> 8;
      L = sat15((L * mg) >> 11), R = sat15((R * mg) >> 11);
      out[2 * (b0 + i)] += L;
      out[2 * (b0 + i) + 1] += R;

      /* ---- the scope */
      int32_t mono = (L + R) >> 1;
      if (sc_state == 0) { /* armed: wait for a rising zero crossing (or give up and start anyway) */
        if ((sc_prev < 0 && mono >= 0) || ++sc_wait > 4000) sc_state = 1, sc_pos = 0, sc_cnt = 0, sc_wait = 0;
      }
      if (sc_state == 1 && ++sc_cnt >= sc_dec) {
        sc_cnt = 0;
        dsp_scope[sc_pos] = (int16_t)(mono >> 1);
        if (++sc_pos >= SCOPE_N) sc_state = 2, dsp_scope_seq++;
      }
      sc_prev = mono;
    }
  }
  /* a scope frame spans ~3 periods of DRONE I */
  {
    uint32_t period = 0xFFFFFFFFu / (osc[0].inc | 1u);
    sc_dec = (uint8_t)clampi((int32_t)(period * 3u / SCOPE_N), 1, 24);
  }
}

/* the panel asks for the next scope frame */
void dsp_scope_rearm(void) {
  if (sc_state == 2) sc_state = 0;
}
