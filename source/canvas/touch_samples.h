// Every touch sample HID took since the last frame. HID samples the touch screen faster than the frame rate,
// so a stroke takes all of them instead of only the newest.
#pragma once
#include <stdbool.h>

#define TOUCH_SAMPLES_MAX 8 // the most touch_samples_read returns: HID's touch ring holds 8

// The samples since the last call, into xs and ys. reset: only remember where HID's ring is.
int touch_samples_read(int *xs, int *ys, bool reset);
