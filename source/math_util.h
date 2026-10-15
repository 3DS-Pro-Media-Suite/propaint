// Angles, the size of a mebibyte, and the clamp, min and max helpers several modules share.
#pragma once

#define PI 3.14159265f
#define TWO_PI (2 * PI)
#define DEGREES_TO_RADIANS (PI / 180)
// Shorter factors for brush import and the colour pop-up: the exact ones differ by a few float steps, which
// would change some imported angles, picked hues and drawn positions.
#define DEGREES_TO_RADIANS_ROUNDED 0.0174533f
#define RADIANS_TO_DEGREES_ROUNDED 57.29578f
#define FULL_TURN_DEGREES 360

#define MEBIBYTE (1 << 20)

static inline int min_int(int a, int b)
{
	return a < b ? a : b;
}

static inline int max_int(int a, int b)
{
	return a > b ? a : b;
}

static inline float min_float(float a, float b)
{
	return a < b ? a : b;
}

static inline float max_float(float a, float b)
{
	return a > b ? a : b;
}

static inline int clamp_int(int value, int low, int high)
{
	if (value < low) {
		return low;
	}
	if (value > high) {
		return high;
	}
	return value;
}

static inline float clamp_float(float value, float low, float high)
{
	if (value < low) {
		return low;
	}
	if (value > high) {
		return high;
	}
	return value;
}

static inline float clamp_unit(float value)
{
	return clamp_float(value, 0, 1);
}
