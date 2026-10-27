/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for DUODRONE's DSP: renders every factory preset to a WAV file and checks the levels, then sweeps
 * every parameter through its range at random (a fuzz) to catch overflows and runaway feedback.
 *
 *   cc -O1 -g -fsanitize=undefined,address -fno-sanitize-recover -Isrc -Iplatform \
 *      tests/render.c src/dsp.c src/presets.c -lm -o build/render && build/render build/render-out
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "patch.h"
#include "presets.h"

/* the fm1.h helpers dsp.c uses (the same sine as platform/common.c) */
int32_t sine(uint32_t phase) {
  int32_t x = (int32_t)phase >> 16, ax = x < 0 ? -x : x, y, ay;
  y = (x * (32768 - ax)) >> 13;
  ay = y < 0 ? -y : y;
  y += ((((y * ay) >> 15) - y) * 7373) >> 15;
  return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static void wav(const char *path, const int16_t *s, uint32_t frames) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  uint32_t data = frames * 4, riff = 36 + data, rate = 44100, brate = rate * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&brate, 4, 1, f);
  fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(s, 2, frames * 2, f);
  fclose(f);
}

static void load(const int16_t *p) {
  for (int i = 0; i < P_COUNT; i++) PAR[i] = p[i];
}

#define SECS 8
#define FR (44100 * SECS)

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  static int32_t blk[512];
  static int16_t out[FR * 2];
  int fail = 0;
  double total_ms = 0;
  printf("%-14s %8s %8s %8s %8s\n", "preset", "peak", "rms", "dc", "clip%");
  for (int k = 0; k < FACTORY_COUNT; k++) {
    load(FACTORY[k].p);
    dsp_init();
    dsp_gate(1);
    clock_t t0 = clock();
    for (uint32_t f = 0; f < FR; f += 256) {
      memset(blk, 0, sizeof blk);
      dsp_render(blk, 256, 0);
      for (int i = 0; i < 512 && f * 2 + i < FR * 2; i++) out[f * 2 + i] = (int16_t)blk[i];
      if (f % 4096 == 0) dsp_scope_rearm();
    }
    total_ms += (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    int64_t sum = 0, sq = 0;
    int peak = 0, clip = 0;
    for (uint32_t i = 44100 * 2; i < FR * 2; i++) { /* skip the first second (fade in) */
      int v = out[i];
      sum += v, sq += (int64_t)v * v;
      if (abs(v) > peak) peak = abs(v);
      if (abs(v) >= 32700) clip++;
    }
    uint32_t n = FR * 2 - 44100 * 2;
    double rms = sqrt((double)sq / n);
    double dc = (double)sum / n, cp = 100.0 * clip / n;
    printf("%-14s %8d %8.0f %8.1f %8.3f\n", FACTORY[k].name, peak, rms, dc, cp);
    if (rms < 800 || cp > 2.0 || dc > 600 || dc < -600) fail = 1, printf("   ^ level out of range\n");
    char path[512];
    snprintf(path, sizeof path, "%s/%d-%s.wav", dir, k + 1, FACTORY[k].name);
    for (char *c = path + strlen(dir) + 1; *c; c++)
      if (*c == ' ') *c = '_';
    wav(path, out, FR);
  }
  printf("render: %.1f ms of CPU for %d s of audio per preset (host)\n", total_ms / FACTORY_COUNT, SECS);

  /* fuzz: random settings every block, extremes included */
  srand(7);
  load(FACTORY[0].p);
  dsp_init();
  dsp_gate(1);
  static const int16_t LO[P_COUNT] = {[P_OLFO1] = -512, [P_OLFO2] = -512, [P_FLFO1] = -512, [P_FLFO2] = -512, [P_DLFO] = -512,
                                      [P_PITCH1] = PITCH_MIN, [P_PITCH2] = PITCH_MIN};
  int16_t HI[P_COUNT];
  for (int i = 0; i < P_COUNT; i++) HI[i] = 1023;
  HI[P_OLFO1] = HI[P_OLFO2] = HI[P_FLFO1] = HI[P_FLFO2] = HI[P_DLFO] = 511;
  HI[P_PITCH1] = HI[P_PITCH2] = PITCH_MAX;
  for (int i = P_SUB1; i <= P_FT2; i++) HI[i] = 1;
  HI[P_LW1] = HI[P_LW2] = LFO_SHAPES - 1;
  HI[P_OD1] = HI[P_OD2] = HI[P_FD1] = HI[P_FD2] = HI[P_DSRC] = 1;
  HI[P_RTYPE] = REVERB_TYPES - 1;
  int32_t aux[512];
  int peak = 0, ffail = 0;
  for (int b = 0; b < 20000; b++) {
    int p = rand() % P_COUNT, r = rand() % 4;
    PAR[p] = (int16_t)(r == 0 ? LO[p] : r == 1 ? HI[p] : LO[p] + rand() % (HI[p] - LO[p] + 1));
    if (b % 500 == 0) dsp_lfo_reset(), dsp_gate(rand() & 1);
    for (int i = 0; i < 512; i++) aux[i] = (rand() % 65535) - 32767;
    memset(blk, 0, sizeof blk);
    dsp_render(blk, 256, (b & 1) ? aux : 0);
    for (int i = 0; i < 512; i++) {
      if (blk[i] > 32767 || blk[i] < -32767) ffail = 1;
      if (abs(blk[i]) > peak) peak = abs(blk[i]);
    }
    dsp_scope_rearm();
  }
  printf("fuzz: 20000 blocks, peak %d, %s\n", peak, ffail ? "FAILED" : "ok");
  return fail | ffail;
}
