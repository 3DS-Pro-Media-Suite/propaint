// CPU drawing on framebuffer-shaped surfaces: pixels, rectangles, rings, lines, discs, icons, text and the UI
// colours.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include "framebuffer.h"

// Framebuffer-shaped pixels (see framebuffer.h), SCREEN_HEIGHT rows.
typedef struct {
	u8 *pixels;
	int width;
} Surface;

#define RGB_RED_SHIFT 16
#define RGB_GREEN_SHIFT 8
#define RGB(r, g, b) (((u32)(r) << RGB_RED_SHIFT) | ((u32)(g) << RGB_GREEN_SHIFT) | (u32)(b))
#define COLOR_RAIL RGB(22, 22, 27)
#define COLOR_SHEET RGB(30, 30, 36)
#define COLOR_WIDGET RGB(48, 48, 57)
#define COLOR_WIDGET_LIGHT RGB(70, 70, 82)
#define COLOR_TEXT RGB(236, 232, 222)
#define COLOR_DIM RGB(150, 148, 142)
#define COLOR_DISABLED RGB(92, 92, 102)
#define COLOR_ACCENT RGB(64, 200, 188)
#define COLOR_ACCENT_DARK RGB(30, 78, 76)
#define COLOR_WARNING RGB(236, 84, 72)
#define COLOR_WHITE RGB(255, 255, 255)
#define COLOR_BLACK RGB(0, 0, 0)

// The rows drawing is limited to, for the scrolling lists. Set them with draw_set_clip.
extern int g_clipTop;
extern int g_clipBottom;

static inline void draw_set_clip(int top, int bottom)
{
	g_clipTop = top;
	g_clipBottom = bottom;
}

// Drawing may use the whole screen again.
static inline void draw_reset_clip(void)
{
	draw_set_clip(0, SCREEN_HEIGHT);
}

// Lays a colour with alpha 0..255 over a framebuffer pixel, which is BGR.
static inline void blend_bgr_pixel(u8 *pixel, u8 red, u8 green, u8 blue, int alpha)
{
	if (alpha >= 255) {
		pixel[0] = blue;
		pixel[1] = green;
		pixel[2] = red;
	} else {
		pixel[0] = (pixel[0] * (255 - alpha) + blue * alpha) / 255;
		pixel[1] = (pixel[1] * (255 - alpha) + green * alpha) / 255;
		pixel[2] = (pixel[2] * (255 - alpha) + red * alpha) / 255;
	}
}

static inline void draw_pixel(Surface *surface, int x, int y, u32 color, int alpha)
{
	if ((unsigned)x >= (unsigned)surface->width || y < g_clipTop || y >= g_clipBottom) {
		return;
	}
	u8 *pixel =
	        surface->pixels + ((size_t)x * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y)) * FRAMEBUFFER_PIXEL_BYTES;
	blend_bgr_pixel(pixel, color >> RGB_RED_SHIFT, color >> RGB_GREEN_SHIFT, color, alpha);
}

enum {
	FONT_SMALL,
	FONT_LARGE
};

#define DRAW_TEXT_SIZE 96 // the longest text draw_text shows, with its terminating zero

enum {
	ALIGN_LEFT,
	ALIGN_CENTER,
	ALIGN_RIGHT
};

void draw_fill(Surface *surface, int x, int y, int width, int height, u32 color, int alpha);
void draw_frame(Surface *surface, int x, int y, int width, int height, u32 color); // a one pixel outline
void draw_rounded_rect(Surface *surface, int x, int y, int width, int height, u32 color); // corner radius 4
void draw_ring(Surface *surface, float centerX, float centerY, float radius, u32 color, int alpha);
void draw_line(Surface *surface, float x0, float y0, float x1, float y1, u32 color);
void draw_disc(Surface *surface, float centerX, float centerY, float radius, u32 color); // anti-aliased
void draw_icon(Surface *surface, int icon, int x, int y, u32 color);
void draw_image(Surface *surface, int x, int y, const u8 *rgb, int width, int height);
int draw_text_width(int font, const char *text);
int draw_line_height(int font);
void draw_text(Surface *surface, int font, int x, int y, u32 color, int align, const char *format, ...);
// A formatted string that stays valid until four more draw_format calls.
const char *draw_format(const char *format, ...);
// Copies name into out, cut short to fit maxWidth pixels in the small font. It does not end in a space.
void draw_fit_text(char *out, size_t outSize, const char *name, int maxWidth);
u32 draw_color(const u8 rgb[3]);
bool draw_is_light(const u8 rgb[3]); // light enough for black text
