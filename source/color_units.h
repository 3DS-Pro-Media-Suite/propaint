// Colour values as bytes (0..255), units (0..1) and percentages, and the size of an RGB pixel.
#pragma once

#define PERCENT_TO_UNIT 0.01f
#define BYTE_TO_UNIT (1.0f / 255.0f)
#define UNIT_TO_BYTE 255.0f
#define RGB_CHANNELS 3
#define RGB_PIXEL_BYTES RGB_CHANNELS // one byte per channel
