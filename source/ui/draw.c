#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "ascii.h"
#include "color_units.h"
#include "font.h"
#include "icons.h"
#include "math_util.h"

#define CORNER_ROWS 4 // rows of a rounded corner

// About one ring point per pixel of circumference (a little over 2 * PI per radius), so the ring has no gaps.
#define RING_POINTS_PER_RADIUS 6.3f
#define RING_EXTRA_POINTS 8

#define DISC_MARGIN 1 // px around the disc's bounds, for its anti-aliased edge
#define HALF_PIXEL 0.5f

#define FONT_FIRST_CHAR ASCII_PRINTABLE_FIRST // the font tables start at the space
#define GLYPH_LEFT_SHIFT 1 // glyph cells start one column left of the pen

#define FORMAT_SLOT_COUNT 4 // a power of two
#define FORMAT_SLOT_SIZE 40

#define LIGHT_LUMA 150 // above this, black text reads better than white

// How far each row of a rounded corner is inset, from the outer row in.
static const int CORNER_INSET[CORNER_ROWS] = {3, 1, 1, 0};

int g_clipTop = 0;
int g_clipBottom = SCREEN_HEIGHT;

void draw_fill(Surface *surface, int x, int y, int width, int height, u32 color, int alpha)
{
	int right = x + width;
	int bottom = y + height;
	if (x < 0) {
		x = 0;
	}
	if (y < g_clipTop) {
		y = g_clipTop;
	}
	if (right > surface->width) {
		right = surface->width;
	}
	if (bottom > g_clipBottom) {
		bottom = g_clipBottom;
	}
	u8 red = color >> RGB_RED_SHIFT;
	u8 green = color >> RGB_GREEN_SHIFT;
	u8 blue = color;
	for (int column = x; column < right; column++) {
		u8 *pixel = surface->pixels +
		            ((size_t)column * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y)) * FRAMEBUFFER_PIXEL_BYTES;
		for (int row = y; row < bottom; row++) {
			blend_bgr_pixel(pixel, red, green, blue, alpha);
			// Columns are stored bottom-up, so the next row down is the previous pixel.
			pixel -= FRAMEBUFFER_PIXEL_BYTES;
		}
	}
}

void draw_frame(Surface *surface, int x, int y, int width, int height, u32 color)
{
	draw_fill(surface, x, y, width, 1, color, 255);
	draw_fill(surface, x, y + height - 1, width, 1, color, 255);
	draw_fill(surface, x, y, 1, height, color, 255);
	draw_fill(surface, x + width - 1, y, 1, height, color, 255);
}

static int corner_inset(int row, int height)
{
	if (row < CORNER_ROWS) {
		return CORNER_INSET[row];
	}
	if (row >= height - CORNER_ROWS) {
		return CORNER_INSET[height - 1 - row];
	}
	return 0;
}

void draw_rounded_rect(Surface *surface, int x, int y, int width, int height, u32 color)
{
	for (int row = 0; row < height; row++) {
		int inset = corner_inset(row, height);
		draw_fill(surface, x + inset, y + row, width - 2 * inset, 1, color, 255);
	}
}

void draw_ring(Surface *surface, float centerX, float centerY, float radius, u32 color, int alpha)
{
	int pointCount = (int)(radius * RING_POINTS_PER_RADIUS) + RING_EXTRA_POINTS;
	for (int i = 0; i < pointCount; i++) {
		float angle = i * TWO_PI / pointCount;
		int x = (int)floorf(centerX + cosf(angle) * radius);
		int y = (int)floorf(centerY + sinf(angle) * radius);
		draw_pixel(surface, x, y, color, alpha);
	}
}

void draw_line(Surface *surface, float x0, float y0, float x1, float y1, u32 color)
{
	int steps = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
	for (int i = 0; i <= steps; i++) {
		int x = (int)floorf(x0 + (x1 - x0) * i / steps);
		int y = (int)floorf(y0 + (y1 - y0) * i / steps);
		draw_pixel(surface, x, y, color, 255);
	}
}

void draw_disc(Surface *surface, float centerX, float centerY, float radius, u32 color)
{
	int left = (int)floorf(centerX - radius - DISC_MARGIN);
	int right = (int)ceilf(centerX + radius + DISC_MARGIN);
	int top = (int)floorf(centerY - radius - DISC_MARGIN);
	int bottom = (int)ceilf(centerY + radius + DISC_MARGIN);
	for (int y = top; y <= bottom; y++) {
		for (int x = left; x <= right; x++) {
			float offsetX = x + 0.5f - centerX;
			float offsetY = y + 0.5f - centerY;
			// Distance of the pixel centre outside the edge: within half a pixel it is partly covered.
			float outside = sqrtf(offsetX * offsetX + offsetY * offsetY) - radius;
			if (outside < HALF_PIXEL) {
				int alpha = outside < -HALF_PIXEL ? 255 : (int)((HALF_PIXEL - outside) * 255);
				draw_pixel(surface, x, y, color, alpha);
			}
		}
	}
}

void draw_icon(Surface *surface, int icon, int x, int y, u32 color)
{
	const u8 *coverage = ICON_BITMAPS[icon];
	for (int row = 0; row < ICON_SIZE; row++) {
		for (int column = 0; column < ICON_SIZE; column++) {
			int alpha = coverage[row * ICON_SIZE + column];
			if (alpha) {
				draw_pixel(surface, x + column, y + row, color, alpha);
			}
		}
	}
}

static int glyph_advance(int font, char character)
{
	int glyph = character - FONT_FIRST_CHAR;
	return font != FONT_SMALL ? FONT_LARGE_ADVANCES[glyph] : FONT_SMALL_ADVANCES[glyph];
}

int draw_text_width(int font, const char *text)
{
	int width = 0;
	for (; *text; text++) {
		if (ascii_is_printable(*text)) {
			width += glyph_advance(font, *text);
		}
	}
	return width;
}

int draw_line_height(int font)
{
	return font != FONT_SMALL ? FONT_LARGE_HEIGHT : FONT_SMALL_HEIGHT;
}

static void draw_glyph(Surface *surface, const u8 *coverage, int glyphWidth, int glyphHeight, int x, int y,
                       u32 color)
{
	for (int column = 0; column < glyphWidth; column++) {
		for (int row = 0; row < glyphHeight; row++) {
			int alpha = coverage[row * glyphWidth + column];
			if (alpha) {
				draw_pixel(surface, x + column - GLYPH_LEFT_SHIFT, y + row, color, alpha);
			}
		}
	}
}

void draw_text(Surface *surface, int font, int x, int y, u32 color, int align, const char *format, ...)
{
	char text[DRAW_TEXT_SIZE];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(text, sizeof text, format, arguments);
	va_end(arguments);
	int width = draw_text_width(font, text);
	if (align == ALIGN_CENTER) {
		x -= width / 2;
	} else if (align == ALIGN_RIGHT) {
		x -= width;
	}
	bool large = font != FONT_SMALL;
	int glyphWidth = large ? FONT_LARGE_WIDTH : FONT_SMALL_WIDTH;
	int glyphHeight = large ? FONT_LARGE_HEIGHT : FONT_SMALL_HEIGHT;
	for (const char *character = text; *character; character++) {
		if (!ascii_is_printable(*character)) {
			continue;
		}
		int glyph = *character - FONT_FIRST_CHAR;
		const u8 *coverage = large ? FONT_LARGE_GLYPHS[glyph] : FONT_SMALL_GLYPHS[glyph];
		draw_glyph(surface, coverage, glyphWidth, glyphHeight, x, y, color);
		x += glyph_advance(font, *character);
	}
}

const char *draw_format(const char *format, ...)
{
	static char slots[FORMAT_SLOT_COUNT][FORMAT_SLOT_SIZE];
	static int slot;
	slot = (slot + 1) & (FORMAT_SLOT_COUNT - 1);
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(slots[slot], sizeof slots[slot], format, arguments);
	va_end(arguments);
	return slots[slot];
}

u32 draw_color(const u8 rgb[3])
{
	return RGB(rgb[0], rgb[1], rgb[2]);
}

bool draw_is_light(const u8 rgb[3])
{
	// Luma times ten, with the weights 0.3, 0.6 and 0.1.
	int lumaTimesTen = rgb[0] * 3 + rgb[1] * 6 + rgb[2];
	return lumaTimesTen > LIGHT_LUMA * 10;
}

void draw_image(Surface *surface, int x, int y, const u8 *rgb, int width, int height)
{
	for (int row = 0; row < height; row++) {
		for (int column = 0; column < width; column++) {
			const u8 *source = rgb + (row * width + column) * RGB_PIXEL_BYTES;
			draw_pixel(surface, x + column, y + row, RGB(source[0], source[1], source[2]), 255);
		}
	}
}

void draw_fit_text(char *out, size_t outSize, const char *name, int maxWidth)
{
	snprintf(out, outSize, "%s", name);
	int length = strlen(out);
	while (length > 0 && draw_text_width(FONT_SMALL, out) > maxWidth) {
		length--;
		out[length] = 0;
	}
	while (length > 0 && out[length - 1] == ' ') {
		length--;
		out[length] = 0;
	}
}
