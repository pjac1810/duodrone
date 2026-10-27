/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DUODRONE_PRESETS_H
#define DUODRONE_PRESETS_H
#include "patch.h"

#define NAME_LEN 12
#define FACTORY_COUNT 8
#define USER_COUNT 8

typedef struct {
  char name[NAME_LEN + 1];
  int16_t p[P_COUNT];
} preset_t;

extern const preset_t FACTORY[FACTORY_COUNT];

#endif
