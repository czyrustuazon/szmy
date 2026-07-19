#pragma once

#include <stdint.h>

typedef struct {
    int      ready;
    int      armed;
    int      count;
    int16_t  prev_x;
    int16_t  prev_y;
    int16_t  prev_z;
    uint64_t last_tap_ms;
} tap_detector_t;

void tap_detector_reset(tap_detector_t *detector);

/* Feed one accelerometer sample. Returns 1 exactly once per triple tap.
 * now_ms must be monotonic. */
int tap_detector_feed(tap_detector_t *detector,
                      int16_t x, int16_t y, int16_t z, uint64_t now_ms);
