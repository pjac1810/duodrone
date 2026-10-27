/* SPDX-License-Identifier: GPL-3.0-only */
/* DUODRONE: a two-voice analogue-style drone synthesizer for the M-VAVE FM-1, after the layout of a compact
 * portable modular drone synth (two DRONE voices, an ULTRA LFO, a MIXER with delay and reverb).
 *
 * The panel (see README.md for the full map):
 *   HOME            PERFORM page: CUTOFF I, CUTOFF II, LFO RATE, REVERB MIX
 *   SCL             DRONE I  (again: OSC <-> FILTER)
 *   ENV             DRONE II (again: OSC <-> FILTER)
 *   LFO             ULTRA LFO (again: its routing switches)
 *   EDIT            the switches: SUB, LFO ROUTE, SYNC, LINK, LP/HP
 *   FX              DELAY (again: REVERB)
 *   GLO             MIXER: VOLUME I, VOLUME II, AUX IN, MAIN OUT
 *   KNOB1..4        the page's four controls          SELECT / ALGORITHM   DRONE I / DRONE II pitch
 *   PRESETS         the sound: F1..F8 factory, U1..U8 yours      SAVE   store into a U slot
 *   keys            set the drones' pitch (SEQ: which drone, or GATE to play)     OCT- OCT+   the keys' octave
 *   PLAY            the drones on / off        ARP   LFO reset        REC (held)   fine steps
 *   MASTER          the volume
 *   USB-MIDI / TRS  notes set the pitch, CC 20..59 the parameters; USB audio in = AUX IN */
#include "fm1.h"
#include "patch.h"
#include "presets.h"

/* ---------------------------------------------------------------- colours */

#define C_BG RGB(12, 12, 14)
#define C_HEAD RGB(24, 24, 28)
#define C_TEXT RGB(234, 228, 212)
#define C_DIM RGB(118, 114, 106)
#define C_FAINT RGB(54, 54, 60)
#define C_KNOB RGB(40, 40, 46)
#define C_RIM RGB(96, 96, 104)
#define C_D1 RGB(255, 146, 48)  /* DRONE I: amber */
#define C_D2 RGB(72, 204, 224)  /* DRONE II: cyan */
#define C_LFO RGB(186, 132, 255) /* ULTRA LFO: violet */
#define C_SW RGB(236, 92, 112)   /* switches: red */
#define C_FX RGB(120, 214, 128)  /* MIXER and effects: green */
#define C_MIXR RGB(240, 206, 96) /* mixer: gold */
#define C_SCOPE RGB(150, 230, 170)

static uint16_t mix565(uint16_t a, uint16_t b, int t) { /* t 0..256: a -> b */
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((ar + (((br - ar) * t) >> 8)) << 11) | ((ag + (((bg - ag) * t) >> 8)) << 5) | (ab + (((bb - ab) * t) >> 8)));
}

/* ---------------------------------------------------------------- the parameters */

enum { K_KNOB, K_BIP, K_PITCH, K_SW, K_LIST };

typedef struct {
  const char *label;
  uint8_t kind;
  int16_t lo, hi;
  const char *const *names;
} pdef_t;

static const char *const N_SUB[] = {"-1 OCT", "-2 OCT"};
static const char *const N_ROUTE[] = {"X-WAVE", "PITCH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_FT[] = {"LP", "HP"};
static const char *const N_WAVE[] = {"WAVE I", "WAVE II"};
static const char *const N_LFO[LFO_SHAPES] = {"OFF", "TRI", "SQUARE", "SAW", "TRI 90", "SQR 90", "TRI x2",
                                              "SAW x2", "SQP x2", "SQP x4", "EXP SAW", "EXPS x2", "EXUD x4", "LOG TRI"};
static const char *const N_RV[REVERB_TYPES] = {"CH/HALL", "HALL 1", "HALL 2", "PLATE", "ROOM", "CATHED", "SPRING", "ENDLESS"};

static const pdef_t PD[P_COUNT] = {
    [P_XW1] = {"X-WAVE", K_KNOB, 0, 1023, 0},       [P_MIX1] = {"OSC MIX", K_KNOB, 0, 1023, 0},
    [P_OLFO1] = {"OSC LFO", K_BIP, -512, 511, 0},   [P_PITCH1] = {"PITCH", K_PITCH, PITCH_MIN, PITCH_MAX, 0},
    [P_CUT1] = {"CUTOFF", K_KNOB, 0, 1023, 0},      [P_RES1] = {"RESO", K_KNOB, 0, 1023, 0},
    [P_FLFO1] = {"FLT LFO", K_BIP, -512, 511, 0},   [P_VOL1] = {"VOLUME", K_KNOB, 0, 1023, 0},
    [P_XW2] = {"X-WAVE", K_KNOB, 0, 1023, 0},       [P_MIX2] = {"OSC MIX", K_KNOB, 0, 1023, 0},
    [P_OLFO2] = {"OSC LFO", K_BIP, -512, 511, 0},   [P_PITCH2] = {"PITCH", K_PITCH, PITCH_MIN, PITCH_MAX, 0},
    [P_CUT2] = {"CUTOFF", K_KNOB, 0, 1023, 0},      [P_RES2] = {"RESO", K_KNOB, 0, 1023, 0},
    [P_FLFO2] = {"FLT LFO", K_BIP, -512, 511, 0},   [P_VOL2] = {"VOLUME", K_KNOB, 0, 1023, 0},
    [P_SUB1] = {"SUB I", K_SW, 0, 1, N_SUB},        [P_SUB2] = {"SUB II", K_SW, 0, 1, N_SUB},
    [P_ROUTE1] = {"LFO>I", K_SW, 0, 1, N_ROUTE},    [P_ROUTE2] = {"LFO>II", K_SW, 0, 1, N_ROUTE},
    [P_SYNC] = {"SYNC", K_SW, 0, 1, N_ONOFF},       [P_LINK] = {"LINK", K_SW, 0, 1, N_ONOFF},
    [P_FT1] = {"FILT I", K_SW, 0, 1, N_FT},         [P_FT2] = {"FILT II", K_SW, 0, 1, N_FT},
    [P_LW1] = {"WAVE I", K_LIST, 0, LFO_SHAPES - 1, N_LFO},
    [P_LW2] = {"WAVE II", K_LIST, 0, LFO_SHAPES - 1, N_LFO},
    [P_RATE] = {"RATE", K_KNOB, 0, 1023, 0},
    [P_OD1] = {"OSC I", K_SW, 0, 1, N_WAVE},        [P_OD2] = {"OSC II", K_SW, 0, 1, N_WAVE},
    [P_FD1] = {"FLT I", K_SW, 0, 1, N_WAVE},        [P_FD2] = {"FLT II", K_SW, 0, 1, N_WAVE},
    [P_DMIX] = {"MIX", K_KNOB, 0, 1023, 0},         [P_DTIME] = {"TIME", K_KNOB, 0, 1023, 0},
    [P_DREP] = {"REPEATS", K_KNOB, 0, 1023, 0},     [P_DLFO] = {"LFO", K_BIP, -512, 511, 0},
    [P_DSRC] = {"DLY SRC", K_SW, 0, 1, N_WAVE},
    [P_RMIX] = {"MIX", K_KNOB, 0, 1023, 0},         [P_RTYPE] = {"TYPE", K_LIST, 0, REVERB_TYPES - 1, N_RV},
    [P_AUX] = {"AUX IN", K_KNOB, 0, 1023, 0},       [P_MAIN] = {"MAIN", K_KNOB, 0, 1023, 0},
};

/* ---------------------------------------------------------------- the pages */

typedef struct {
  const char *title;
  uint16_t color;
  int8_t p[4];
} page_t;

enum { PG_HOME, PG_D1A, PG_D1B, PG_D2A, PG_D2B, PG_LFOA, PG_LFOB, PG_SWA, PG_SWB, PG_DLY, PG_RV, PG_MIX, PG_COUNT };

static const page_t PAGES[PG_COUNT] = {
    [PG_HOME] = {"PERFORM", C_TEXT, {P_CUT1, P_CUT2, P_RATE, P_RMIX}},
    [PG_D1A] = {"DRONE I . OSC", C_D1, {P_XW1, P_MIX1, P_OLFO1, P_PITCH1}},
    [PG_D1B] = {"DRONE I . FILTER", C_D1, {P_CUT1, P_RES1, P_FLFO1, P_VOL1}},
    [PG_D2A] = {"DRONE II . OSC", C_D2, {P_XW2, P_MIX2, P_OLFO2, P_PITCH2}},
    [PG_D2B] = {"DRONE II . FILTER", C_D2, {P_CUT2, P_RES2, P_FLFO2, P_VOL2}},
    [PG_LFOA] = {"ULTRA LFO", C_LFO, {P_LW1, P_LW2, P_RATE, -1}},
    [PG_LFOB] = {"LFO ROUTING", C_LFO, {P_OD1, P_OD2, P_FD1, P_FD2}},
    [PG_SWA] = {"SWITCHES . OSC", C_SW, {P_SUB1, P_SUB2, P_ROUTE1, P_ROUTE2}},
    [PG_SWB] = {"SWITCHES . VOICE", C_SW, {P_SYNC, P_LINK, P_FT1, P_FT2}},
    [PG_DLY] = {"DELAY", C_FX, {P_DMIX, P_DTIME, P_DREP, P_DLFO}},
    [PG_RV] = {"REVERB", C_FX, {P_RMIX, P_RTYPE, P_DSRC, -1}},
    [PG_MIX] = {"MIXER", C_MIXR, {P_VOL1, P_VOL2, P_AUX, P_MAIN}},
};

/* a page button and the pages it steps through */
static const struct {
  uint8_t btn, a, b;
} PAGE_BTN[] = {
    {BTN_HOME, PG_HOME, PG_HOME}, {BTN_SEL, PG_D1A, PG_D1B}, {BTN_ENV, PG_D2A, PG_D2B}, {BTN_LFO, PG_LFOA, PG_LFOB},
    {BTN_EDIT, PG_SWA, PG_SWB},   {BTN_FX, PG_DLY, PG_RV},   {BTN_GLO, PG_MIX, PG_MIX},
};
#define PAGE_BTNS (int)(sizeof PAGE_BTN / sizeof PAGE_BTN[0])

/* ---------------------------------------------------------------- what is kept in flash */

#define STORE_MAGIC 0x44524E32u /* "DRN2" */
enum { KM_I, KM_II, KM_BOTH, KM_GATE, KM_COUNT };
static const char *const N_KM[KM_COUNT] = {"I", "II", "I+II", "GATE"};

static struct {
  uint32_t magic;
  uint8_t cur;    /* 0..7 factory, 8..15 user */
  int8_t oct;     /* the keys' octave, -3..3 */
  uint8_t keymode;
  uint8_t pad;
  preset_t user[USER_COUNT];
} store;

/* ---------------------------------------------------------------- the panel's state */

static uint8_t page = PG_HOME, drone_on = 1, modified, saving, save_slot;
static uint32_t held_midi; /* notes held over MIDI (GATE mode), a count */
static char cur_name[NAME_LEN + 1];

/* what is on the screen, to redraw only what changed */
static int32_t shown_col[4] = {-99999, -99999, -99999, -99999};
static int8_t shown_page = -1;
static uint32_t shown_head = 0xFFFFFFFFu, shown_foot = 0xFFFFFFFFu, shown_lamp = 0xFFFFFFFFu;
static uint32_t scope_seq_drawn, scope_t, lamp_t;
static uint32_t led_keys_now = 0xFFFFFFFFu, led_btn_now = 0xFFFFFFFFu;

/* ---------------------------------------------------------------- small helpers */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static char *put(char *d, const char *s) {
  while (*s) *d++ = *s++;
  *d = 0;
  return d;
}

static char *put_int(char *d, int32_t v) {
  char b[12];
  format_int(b, v);
  return put(d, b);
}

static int center_text(int cx, int y, const char *s, uint16_t c, int font) {
  return text(cx - text_width(s, font) / 2, y, s, c, font);
}

/* 2^(m / 1000) x 1000, for the displays (Hz, ms) */
static int32_t exp2_milli(int32_t m) {
  int32_t oct = m / 1000, r = m - oct * 1000;
  int32_t f = 1000 + (r * (656 + (344 * r) / 1000)) / 1000;
  return oct >= 0 ? f << oct : f >> -oct;
}

static void note_name(char *d, int32_t cents) {
  static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int32_t n = (cents + 50) / 100, off = cents - n * 100;
  d = put(d, NN[n % 12]);
  d = put_int(d, n / 12 - 1);
  if (off) {
    d = put(d, off > 0 ? "+" : "");
    put_int(d, off);
  }
}

/* the value of parameter p as text */
static void value_text(char *d, int p) {
  int32_t v = PAR[p];
  const pdef_t *pd = &PD[p];
  if (pd->names) {
    put(d, pd->names[clampi(v, pd->lo, pd->hi)]);
    return;
  }
  switch (p) {
    case P_PITCH1:
    case P_PITCH2:
      note_name(d, v);
      return;
    case P_XW1:
    case P_XW2:
      if (v < 30) put(d, "TRI");
      else if (v < 312) put_int(put(d, "T>S "), (v * 100) / 341);
      else if (v < 370) put(d, "SAW");
      else if (v < 653) put_int(put(d, "S>Q "), ((v - 341) * 100) / 341);
      else if (v < 711) put(d, "SQUARE");
      else put(put_int(put(d, "PW "), 50 - ((v - 682) * 44) / 341), "%");
      return;
    case P_MIX1:
    case P_MIX2:
      if (v < 24) put(d, "X");
      else if (v > 999) put(d, "SUB");
      else if (v > 488 && v < 536) put(d, "X+S");
      else put_int(put(d, v < 512 ? "X " : "S "), v < 512 ? 100 - (v * 100) / 512 : ((v - 512) * 100) / 511);
      return;
    case P_CUT1:
    case P_CUT2: { /* 20 Hz .. 16 kHz */
      int32_t hz = (20 * exp2_milli((v * 9644) / 1023)) / 1000;
      if (hz < 1000) put(put_int(d, hz), "Hz");
      else {
        d = put_int(d, hz / 1000);
        if (hz < 10000) d = put_int(put(d, "."), (hz % 1000) / 100);
        put(d, "k");
      }
      return;
    }
    case P_RATE: { /* 0.02 .. 70 Hz, in mHz */
      int32_t mhz = (20 * exp2_milli((v * 11773) / 1023)) / 1000;
      if (mhz < 995) { /* 0.02Hz .. 0.99Hz */
        int32_t cs = (mhz + 5) / 10;
        d = put(d, cs < 10 ? "0.0" : "0.");
        put(put_int(d, cs), "Hz");
      } else if (mhz < 9950) { /* 1.0Hz .. 9.9Hz */
        int32_t ds = (mhz + 50) / 100;
        d = put_int(d, ds / 10);
        put(put_int(put(d, "."), ds % 10), "Hz");
      } else {
        put(put_int(d, (mhz + 500) / 1000), "Hz");
      }
      return;
    }
    case P_DTIME: /* the delay's time, ms */
      put(put_int(d, 30 + (920 * v / 1023) * v / 1023), "ms");
      return;
    default:
      break;
  }
  if (pd->kind == K_BIP) {
    int32_t pc = (v * 100) / 511;
    put_int(put(d, pc > 0 ? "+" : ""), clampi(pc, -100, 100));
  } else {
    put_int(d, (v * 100) / 1023);
  }
}

/* ---------------------------------------------------------------- drawing */

static uint32_t isqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) {
    if (v >= r + b) v -= r + b, r = (r >> 1) + b;
    else r >>= 1;
    b >>= 2;
  }
  return r;
}

static void disc(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int hw = (int)isqrt((uint32_t)(r * r - dy * dy));
    rect(cx - hw, cy + dy, 2 * hw + 1, 1, c);
  }
}

#define COL_W 60
#define KNOB_Y 58
#define LABEL_Y 92
#define VALUE_Y 110
#define COLS_Y0 26
#define COLS_H 104
#define SCOPE_Y 134
#define SCOPE_H 60
#define FOOT_Y 198

/* an angle (2^32 = a turn, clockwise from the top) for a knob at t (0..1023) */
static uint32_t knob_angle(int32_t t) { return 0u - 1610612736u + (uint32_t)t * 3148841u; /* -135 deg + t * 270 deg */ }

static void polar(int cx, int cy, uint32_t a, int r, int *x, int *y) {
  *x = cx + ((r * sine(a)) >> 15);
  *y = cy - ((r * sine(a + 1073741824u)) >> 15);
}

static void draw_knob(int cx, int cy, int32_t t, int bip, uint16_t col) {
  /* the ring of lights: 25 dots over 270 degrees, lit from the start (or the centre) to the value */
  int32_t tv = t * 24 / 1023;
  for (int i = 0; i <= 24; i++) {
    int x, y, lit = bip ? ((tv >= 12) ? (i >= 12 && i <= tv) : (i <= 12 && i >= tv)) : i <= tv;
    polar(cx, cy, knob_angle(i * 1023 / 24), 25, &x, &y);
    rect(x - 1, y - 1, 3, 3, lit ? col : C_FAINT);
  }
  disc(cx, cy, 19, C_RIM);
  disc(cx, cy, 17, C_KNOB);
  int x, y, x0, y0;
  polar(cx, cy, knob_angle(t), 15, &x, &y);
  polar(cx, cy, knob_angle(t), 4, &x0, &y0);
  line(x0, y0, x, y, C_TEXT);
  line(x0 + 1, y0, x + 1, y, C_TEXT);
  line(x0, y0 + 1, x, y + 1, C_TEXT);
}

static void draw_switch(int cx, int cy, int v, const char *const *names, uint16_t col) {
  for (int i = 0; i < 2; i++) {
    int y = cy - 21 + i * 22;
    if (i == v) {
      rect(cx - 26, y, 52, 19, col);
      center_text(cx, y + 2, names[i], C_BG, FONT_SMALL);
    } else {
      frame(cx - 26, y, 52, 19, C_FAINT);
      center_text(cx, y + 2, names[i], C_DIM, FONT_SMALL);
    }
  }
}

/* a little picture of an LFO shape */
static void draw_wave(int cx, int cy, int shape, uint16_t col) {
  frame(cx - 26, cy - 18, 52, 36, C_FAINT);
  if (shape == LFO_OFF) {
    line(cx - 22, cy, cx + 22, cy, C_DIM);
    return;
  }
  int px = 0, py = 0;
  for (int i = 0; i <= 44; i++) {
    int y = cy - ((dsp_lfo_shape(shape, (uint32_t)i * 97612893u) * 14) >> 15);
    int x = cx - 22 + i;
    if (i) line(px, py, x, y, col);
    px = x, py = y;
  }
}

/* the reverb type: a stepped knob */
static void draw_column(int c) {
  const page_t *pg = &PAGES[page];
  int x0 = c * COL_W, cx = x0 + COL_W / 2;
  rect(x0, COLS_Y0, COL_W, COLS_H, C_BG);
  int p = pg->p[c];
  if (p < 0) return;
  const pdef_t *pd = &PD[p];
  uint16_t col = pg->color;
  if (page == PG_HOME) col = (p == P_CUT1) ? C_D1 : (p == P_CUT2) ? C_D2 : (p == P_RATE) ? C_LFO : C_FX;
  if (page == PG_MIX) col = (p == P_VOL1) ? C_D1 : (p == P_VOL2) ? C_D2 : C_MIXR;
  int32_t v = PAR[p];
  char buf[24];
  if (pd->kind == K_SW) {
    draw_switch(cx, KNOB_Y, v != 0, pd->names, col);
  } else if (p == P_LW1 || p == P_LW2) {
    draw_wave(cx, KNOB_Y, v, col);
  } else {
    int32_t t = ((v - pd->lo) * 1023) / (pd->hi - pd->lo);
    draw_knob(cx, KNOB_Y, t, pd->kind == K_BIP, col);
  }
  center_text(cx, LABEL_Y, pd->label, C_DIM, FONT_SMALL);
  value_text(buf, p);
  center_text(cx, VALUE_Y, buf, C_TEXT, FONT_SMALL);
  /* VOLUME past ~60 % drives the output stage: say so */
  if ((p == P_VOL1 || p == P_VOL2) && v > 620) rect(cx + 20, VALUE_Y + 4, 6, 6, RGB(255, 70, 50));
}

static void draw_header(void) {
  rect(0, 0, SCREEN_W, 24, C_HEAD);
  const page_t *pg = &PAGES[page];
  int x = text(6, 4, page == PG_HOME ? "DUODRONE" : pg->title, page == PG_HOME ? C_D1 : pg->color, FONT_SMALL);
  if (button_held(BTN_REC)) text(x + 6, 4, "FINE", C_SW, FONT_SMALL);
  char b[24], *d = b;
  d = put(d, store.cur < FACTORY_COUNT ? "F" : "U");
  d = put_int(d, (store.cur % 8) + 1);
  d = put(d, " ");
  d = put(d, cur_name);
  if (modified) put(d, "*");
  int w = text_width(b, FONT_SMALL);
  if (page != PG_HOME && w > 108) { /* a long title: the slot number alone */
    b[2] = modified ? '*' : 0, b[3] = 0;
    w = text_width(b, FONT_SMALL);
  }
  text(SCREEN_W - 6 - w, 4, b, C_TEXT, FONT_SMALL);
}

static void draw_foot(void) {
  rect(0, FOOT_Y, SCREEN_W, SCREEN_H - FOOT_Y, C_BG);
  line(0, FOOT_Y, SCREEN_W - 1, FOOT_Y, C_FAINT);
  char b[24], *d;
  d = put(b, "I ");
  note_name(d, PAR[P_PITCH1]);
  text(6, FOOT_Y + 4, b, PAR[P_SYNC] ? mix565(C_D1, C_BG, 128) : C_D1, FONT_SMALL);
  d = put(b, "II ");
  note_name(d, PAR[P_PITCH2]);
  text(92, FOOT_Y + 4, b, C_D2, FONT_SMALL);
  int x = 178;
  if (PAR[P_LINK]) x = text(x, FOOT_Y + 4, "LNK", C_SW, FONT_SMALL) + 4;
  if (PAR[P_SYNC]) text(x > 206 ? 206 : x, FOOT_Y + 4, "SYN", C_SW, FONT_SMALL);
  /* row 2: lamps (drawn by draw_lamps), octave, keys, on/off */
  text(6, FOOT_Y + 22, "LFO", C_DIM, FONT_SMALL);
  d = put(b, "OCT");
  if (store.oct >= 0) d = put(d, "+");
  put_int(d, store.oct);
  text(92, FOOT_Y + 22, b, C_DIM, FONT_SMALL);
  d = put(b, "K:");
  put(d, N_KM[store.keymode]);
  text(148, FOOT_Y + 22, b, C_DIM, FONT_SMALL);
  int on = store.keymode == KM_GATE || drone_on;
  text(208, FOOT_Y + 22, on ? "ON" : "OFF", on ? C_FX : C_SW, FONT_SMALL);
}

static void draw_lamps(void) {
  for (int w = 0; w < 2; w++) {
    int32_t v = dsp_lfo_out[w];
    int t = (int)((v + 32768) >> 8);
    disc(42 + w * 18, FOOT_Y + 30, 6, mix565(C_FAINT, C_LFO, clampi(t, 0, 256)));
  }
}

static void draw_scope(void) {
  rect(0, SCOPE_Y, SCREEN_W, SCOPE_H, C_BG);
  int mid = SCOPE_Y + SCOPE_H / 2;
  for (int x = 0; x < SCREEN_W; x += 6) pixel(x, mid, C_FAINT);
  int px = 0, py = mid;
  for (int i = 0; i < SCOPE_N; i++) {
    int y = mid - ((dsp_scope[i] * (SCOPE_H / 2 - 2)) >> 14);
    y = clampi(y, SCOPE_Y + 1, SCOPE_Y + SCOPE_H - 2);
    if (i) line(px, py, i, y, C_SCOPE);
    px = i, py = y;
  }
}

static void draw_save(void) {
  rect(0, COLS_Y0, SCREEN_W, COLS_H, C_BG);
  frame(10, COLS_Y0 + 6, 220, 92, C_SW);
  char b[24], *d = put(b, "SAVE TO U");
  put_int(d, save_slot + 1);
  center_text(120, COLS_Y0 + 14, b, C_TEXT, FONT_BIG);
  center_text(120, COLS_Y0 + 50, store.user[save_slot].name[0] ? store.user[save_slot].name : "(empty)", C_DIM, FONT_SMALL);
  center_text(120, COLS_Y0 + 72, "PRESETS slot  SAVE ok", C_DIM, FONT_SMALL);
}

/* ---------------------------------------------------------------- presets */

static void load_patch(const int16_t *p, const char *name) {
  for (int i = 0; i < P_COUNT; i++) PAR[i] = (int16_t)clampi(p[i], PD[i].lo, PD[i].hi);
  for (int i = 0; i < NAME_LEN && (cur_name[i] = name[i]); i++) {
  }
  cur_name[NAME_LEN] = 0;
  modified = 0;
}

static void load_slot(int s) {
  s = (s + 16) % 16;
  store.cur = (uint8_t)s;
  if (s < FACTORY_COUNT) load_patch(FACTORY[s].p, FACTORY[s].name);
  else load_patch(store.user[s - 8].p, store.user[s - 8].name);
  shown_page = -1; /* everything again */
}

static void save_store(void) { settings_save(&store, sizeof store); }

/* ---------------------------------------------------------------- editing */

static void set_param(int p, int32_t v) {
  v = clampi(v, PD[p].lo, PD[p].hi);
  if (v == PAR[p]) return;
  if (p == P_PITCH1 && PAR[P_LINK]) { /* PITCH LINK: DRONE II follows DRONE I's moves */
    PAR[P_PITCH2] = (int16_t)clampi(PAR[P_PITCH2] + (v - PAR[P_PITCH1]), PITCH_MIN, PITCH_MAX);
  }
  PAR[p] = (int16_t)v;
  if (p == P_RTYPE || p == P_LW1 || p == P_LW2) { /* nothing else to do: the DSP follows */
  }
  modified = 1;
}

static void turn(int p, int32_t det) {
  if (p < 0 || !det) return;
  const pdef_t *pd = &PD[p];
  int fine = button_held(BTN_REC);
  int32_t a = det < 0 ? -det : det, step;
  switch (pd->kind) {
    case K_PITCH: step = fine ? 5 : 100; break;
    case K_SW:
    case K_LIST: step = 1; break;
    default: step = fine ? 1 : (a > 1 ? 32 : 16); break;
  }
  set_param(p, PAR[p] + det * step);
}

/* a note from a key or MIDI: retune the drone(s) the keys are set to */
static void play_note(int note) {
  int32_t c = clampi(note * 100, PITCH_MIN, PITCH_MAX);
  switch (store.keymode) {
    case KM_II: set_param(P_PITCH2, c); break;
    case KM_BOTH: { /* both move, keeping their interval */
      int32_t d = c - PAR[P_PITCH1];
      PAR[P_PITCH1] = (int16_t)c;
      PAR[P_PITCH2] = (int16_t)clampi(PAR[P_PITCH2] + d, PITCH_MIN, PITCH_MAX);
      modified = 1;
      break;
    }
    default: set_param(P_PITCH1, c); break;
  }
}

static uint8_t gate_now = 255;
static void update_gate(void) {
  int on = store.keymode == KM_GATE ? (keys() != 0 || held_midi > 0) : drone_on;
  if (on != gate_now) gate_now = (uint8_t)on, dsp_gate(on);
}

/* ---------------------------------------------------------------- MIDI */

static void midi(void) {
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    uint8_t t = st & 0xF0;
    if (t == 0x90 && d2) {
      play_note(d1);
      held_midi++;
    } else if (t == 0x80 || (t == 0x90 && !d2)) {
      if (held_midi) held_midi--;
    } else if (t == 0xB0) {
      if (d1 >= 20 && d1 < 20 + P_COUNT) {
        int p = d1 - 20;
        set_param(p, PD[p].lo + ((PD[p].hi - PD[p].lo) * d2 + 63) / 127);
      } else if (d1 == 123) {
        held_midi = 0;
      }
    }
  }
}

/* ---------------------------------------------------------------- LEDs */

static void leds(void) {
  uint32_t k = 0, b = 0;
  for (int v = 0; v < 2; v++) { /* the keys under the drones' notes light up */
    int n = (PAR[P_PITCH1 + v * VOICE_STRIDE] + 50) / 100 - KEY_FIRST_NOTE - store.oct * 12;
    if (n >= 0 && n < KEY_COUNT) k |= 1u << n;
  }
  for (int i = 0; i < PAGE_BTNS; i++)
    if (page == PAGE_BTN[i].a || page == PAGE_BTN[i].b) b |= 1u << PAGE_BTN[i].btn;
  if (store.keymode == KM_GATE ? gate_now : drone_on) b |= 1u << BTN_PLAY;
  if (dsp_lfo_out[0] > 0) b |= 1u << BTN_ARP; /* the LFO's two lamps */
  if (dsp_lfo_out[1] > 0) b |= 1u << BTN_SEQ;
  if (saving) b |= 1u << BTN_SAVE;
  if (k != led_keys_now) {
    for (int i = 0; i < KEY_COUNT; i++)
      if (((k ^ led_keys_now) >> i) & 1u) led_key(i, (k >> i) & 1u);
    led_keys_now = k;
  }
  if (b != led_btn_now) {
    for (int i = 0; i < BTN_COUNT; i++)
      if (((b ^ led_btn_now) >> i) & 1u) led_button(i, (b >> i) & 1u);
    led_btn_now = b;
  }
}

/* ---------------------------------------------------------------- setup and loop */

void setup(void) {
  if (settings_load(&store, sizeof store) || store.magic != STORE_MAGIC) {
    store.magic = STORE_MAGIC, store.cur = 0, store.oct = -1, store.keymode = KM_I;
    for (int i = 0; i < USER_COUNT; i++) store.user[i] = FACTORY[i]; /* the user slots start as copies */
  }
  store.oct = (int8_t)clampi(store.oct, -3, 3);
  store.keymode = (uint8_t)(store.keymode % KM_COUNT);
  dsp_init();
  load_slot(store.cur);
  led_backlight(1);
  clear(C_BG);
}

static void page_button(int i) {
  if (page == PAGE_BTN[i].a && PAGE_BTN[i].a != PAGE_BTN[i].b) page = PAGE_BTN[i].b;
  else page = PAGE_BTN[i].a;
}

void loop(void) {
  uint32_t now = millis();

  /* ---- buttons */
  if (saving) {
    int32_t e = encoder(ENC_PRESETS);
    if (e) save_slot = (uint8_t)((save_slot + 8 + (e % 8)) % 8), shown_page = -1;
    if (button_pressed(BTN_SAVE) || button_pressed(BTN_OCT_UP)) {
      preset_t *u = &store.user[save_slot];
      for (int i = 0; i < P_COUNT; i++) u->p[i] = PAR[i];
      for (int i = 0; i <= NAME_LEN; i++) u->name[i] = cur_name[i];
      store.cur = (uint8_t)(8 + save_slot);
      modified = 0;
      saving = 0;
      save_store();
      shown_page = -1;
    } else if (buttons() && !button_held(BTN_SAVE) && (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_HOME) ||
                                                        button_pressed(BTN_FX) || button_pressed(BTN_SEL) ||
                                                        button_pressed(BTN_ENV) || button_pressed(BTN_LFO) ||
                                                        button_pressed(BTN_EDIT) || button_pressed(BTN_GLO))) {
      saving = 0, shown_page = -1;
    }
  } else {
    for (int i = 0; i < PAGE_BTNS; i++)
      if (button_pressed(PAGE_BTN[i].btn)) page_button(i);
    if (button_pressed(BTN_SAVE)) saving = 1, save_slot = store.cur >= 8 ? store.cur - 8 : 0, shown_page = -1;
    int32_t e = encoder(ENC_PRESETS);
    if (e) load_slot(store.cur + (e > 0 ? 1 : -1));
  }
  if (button_pressed(BTN_PLAY)) drone_on ^= 1;
  if (button_pressed(BTN_ARP)) dsp_lfo_reset();
  if (button_pressed(BTN_SEQ)) store.keymode = (uint8_t)((store.keymode + 1) % KM_COUNT);
  if (!saving) {
    if (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_OCT_UP)) {
      if (button_held(BTN_OCT_DOWN) && button_held(BTN_OCT_UP)) store.oct = 0;
      else store.oct = (int8_t)clampi(store.oct + (button_pressed(BTN_OCT_UP) ? 1 : -1), -3, 3);
    }
  }

  /* ---- encoders */
  for (int c = 0; c < 4; c++) turn(PAGES[page].p[c], encoder(ENC_KNOB1 + c));
  turn(P_PITCH1, encoder(ENC_SELECT));
  turn(P_PITCH2, encoder(ENC_ALGORITHM));

  /* ---- keys and MIDI */
  for (int k = 0; k < KEY_COUNT; k++)
    if (key_pressed(k)) play_note(KEY_FIRST_NOTE + k + store.oct * 12);
  midi();
  update_gate();
  leds();

  /* ---- the screen: only what changed */
  if (shown_page != (int8_t)page) {
    clear(C_BG);
    shown_page = (int8_t)page;
    shown_head = shown_foot = shown_lamp = 0xFFFFFFFFu;
    for (int c = 0; c < 4; c++) shown_col[c] = -99999;
    scope_seq_drawn = dsp_scope_seq - 1;
  }
  uint32_t head = (uint32_t)store.cur | (uint32_t)modified << 5 | (uint32_t)button_held(BTN_REC) << 6 | (uint32_t)saving << 7;
  if (head != shown_head) shown_head = head, draw_header();
  if (saving) {
    if (shown_col[0] != -1000 - save_slot) {
      draw_save();
      for (int c = 0; c < 4; c++) shown_col[c] = -1000 - save_slot;
    }
  } else {
    for (int c = 0; c < 4; c++) {
      int p = PAGES[page].p[c];
      int32_t v = p < 0 ? -1 : PAR[p];
      if (v != shown_col[c]) shown_col[c] = v, draw_column(c);
    }
  }
  uint32_t foot = (uint32_t)PAR[P_PITCH1] * 31u + (uint32_t)PAR[P_PITCH2] * 1009u + (uint32_t)PAR[P_LINK] * 7u +
                  (uint32_t)PAR[P_SYNC] * 13u + (uint32_t)(store.oct + 3) * 101u + store.keymode * 977u + drone_on * 4093u;
  if (foot != shown_foot) shown_foot = foot, draw_foot(), shown_lamp = 0xFFFFFFFFu;
  if (now - lamp_t >= 40) {
    uint32_t l = (uint32_t)((dsp_lfo_out[0] + 32768) >> 11) | (uint32_t)((dsp_lfo_out[1] + 32768) >> 11) << 8;
    if (l != shown_lamp) shown_lamp = l, draw_lamps();
    lamp_t = now;
  }
  if (now - scope_t >= 80 && dsp_scope_seq != scope_seq_drawn) {
    draw_scope();
    scope_seq_drawn = dsp_scope_seq;
    scope_t = now;
    dsp_scope_rearm();
  }
}

/* ---------------------------------------------------------------- the sound */

void audio(int32_t *out, uint32_t frames) {
#if FOMA_USB_AUDIO
  dsp_render(out, frames, audio_in()); /* a computer playing to the FM-1 over USB is the AUX IN */
#else
  dsp_render(out, frames, 0);
#endif
}
