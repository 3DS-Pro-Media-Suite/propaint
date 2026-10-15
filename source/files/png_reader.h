// Reads PNG images as 8-bit grey, for brush textures. Any PNG libpng reads works: colour turns grey, and
// transparent parts read as white.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	u8 *pixels; // width * height grey bytes, row by row; the caller frees them
	int width;
	int height;
} GreyImage;

// False when the file is not a PNG it can read, or is wider or taller than maxSide.
bool png_read_grey_file(const char *path, int maxSide, GreyImage *image);
