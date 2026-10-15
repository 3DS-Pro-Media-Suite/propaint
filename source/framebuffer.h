// The framebuffers are column-major BGR8: pixel (x, y) is at byte (x * height + height - 1 - y) * 3.
#pragma once
#include <stddef.h>

#define SCREEN_HEIGHT 240 // both screens
#define BOTTOM_SCREEN_WIDTH 320
#define TOP_SCREEN_WIDTH 400
#define FRAMEBUFFER_PIXEL_BYTES 3
#define BOTTOM_FRAMEBUFFER_BYTES ((size_t)BOTTOM_SCREEN_WIDTH * SCREEN_HEIGHT * FRAMEBUFFER_PIXEL_BYTES)
#define TOP_FRAMEBUFFER_BYTES ((size_t)TOP_SCREEN_WIDTH * SCREEN_HEIGHT * FRAMEBUFFER_PIXEL_BYTES)
