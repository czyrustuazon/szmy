#include "tap_detector.h"

#include <stdlib.h>
#include <string.h>

/* 3DS accelerometer gravity is roughly 512 raw units. A tap produces a short
 * multi-axis step; release hysteresis prevents its rebound counting twice. */
#define TAP_JERK_THRESHOLD     420
#define TAP_RELEASE_THRESHOLD  120
#define TAP_DEBOUNCE_MS         90u
#define TAP_SEQUENCE_MS       1200u

void tap_detector_reset(tap_detector_t *detector)
{
    if (detector != NULL)
        memset(detector, 0, sizeof(*detector));
}

int tap_detector_feed(tap_detector_t *detector,
                      int16_t x, int16_t y, int16_t z, uint64_t now_ms)
{
    int jerk;

    if (detector == NULL)
        return 0;
    if (!detector->ready) {
        detector->ready  = 1;
        detector->armed  = 1;
        detector->prev_x = x;
        detector->prev_y = y;
        detector->prev_z = z;
        return 0;
    }

    jerk = abs((int)x - detector->prev_x)
         + abs((int)y - detector->prev_y)
         + abs((int)z - detector->prev_z);
    detector->prev_x = x;
    detector->prev_y = y;
    detector->prev_z = z;

    if (detector->count > 0
        && now_ms - detector->last_tap_ms > TAP_SEQUENCE_MS)
        detector->count = 0;

    if (!detector->armed) {
        if (jerk <= TAP_RELEASE_THRESHOLD
            && now_ms - detector->last_tap_ms >= TAP_DEBOUNCE_MS)
            detector->armed = 1;
        return 0;
    }
    if (jerk < TAP_JERK_THRESHOLD)
        return 0;

    detector->armed      = 0;
    detector->last_tap_ms = now_ms;
    detector->count++;
    if (detector->count < 3)
        return 0;

    detector->count = 0;
    return 1;
}
