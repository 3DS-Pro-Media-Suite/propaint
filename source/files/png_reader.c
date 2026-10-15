#include <png.h>
#include <stdlib.h>
#include <string.h>
#include "png_reader.h"

#define WHITE 255

bool png_read_grey_file(const char *path, int maxSide, GreyImage *image)
{
	png_image png;
	memset(&png, 0, sizeof png);
	png.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&png, path)) {
		return false;
	}
	if (png.width > (png_uint_32)maxSide || png.height > (png_uint_32)maxSide) {
		png_image_free(&png);
		return false;
	}
	png.format = PNG_FORMAT_GRAY;
	u8 *pixels = malloc(PNG_IMAGE_SIZE(png));
	if (!pixels) {
		png_image_free(&png);
		return false;
	}
	// Grey output takes the background's green.
	png_color background = {WHITE, WHITE, WHITE};
	// libpng frees its reader whether this works or not.
	if (!png_image_finish_read(&png, &background, pixels, 0, NULL)) {
		free(pixels);
		return false;
	}
	image->pixels = pixels;
	image->width = (int)png.width;
	image->height = (int)png.height;
	return true;
}
