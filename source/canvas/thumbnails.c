#include "thumbnails.h"
#include "color_units.h"
#include "render.h"

#define THUMBNAILS_ALL 0xFFFF // every bit of g_thumbnailsMarked
#define THUMBNAIL_BYTES (LAYER_THUMBNAIL_WIDTH * LAYER_THUMBNAIL_HEIGHT * RGB_PIXEL_BYTES)
#define THUMBNAIL_CELL_WIDTH (CANVAS_WIDTH / LAYER_THUMBNAIL_WIDTH) // canvas px per thumbnail px
#define THUMBNAIL_CELL_HEIGHT (CANVAS_HEIGHT / LAYER_THUMBNAIL_HEIGHT)
#define CHECKER_SHIFT 2 // the transparency checkerboard has squares of 4 x 4 thumbnail px
#define CHECKER_DARK 0.86f
#define CHECKER_LIGHT 1.0f

static u8 g_thumbnails[MAX_LAYERS][THUMBNAIL_BYTES];
static u16 g_thumbnailsMarked = THUMBNAILS_ALL; // thumbnails to make again, bit i = layer i

void thumbnails_mark_all(void)
{
	g_thumbnailsMarked = THUMBNAILS_ALL;
}

void thumbnails_mark_layer(int layerIndex)
{
	g_thumbnailsMarked |= 1u << layerIndex;
}

// One thumbnail pixel: the layer's colour at the centre of its cell, over the transparency checkerboard.
static void make_thumbnail_pixel(const Layer *layer, int x, int y, u8 *pixel)
{
	float premultiplied[RGB_CHANNELS];
	float alpha = 0;
	if (layer->used) {
		int sampleX = x * THUMBNAIL_CELL_WIDTH + THUMBNAIL_CELL_WIDTH / 2;
		int sampleY = y * THUMBNAIL_CELL_HEIGHT + THUMBNAIL_CELL_HEIGHT / 2;
		render_layer_sample(layer, sampleX, sampleY, premultiplied, &alpha);
	}
	int checkerSquare = (x >> CHECKER_SHIFT) + (y >> CHECKER_SHIFT);
	float background = checkerSquare & 1 ? CHECKER_DARK : CHECKER_LIGHT;
	for (int c = 0; c < RGB_CHANNELS; c++) {
		float color = alpha > 0 ? premultiplied[c] : 0;
		float value = color + background * (1 - alpha);
		pixel[c] = (u8)(value * 255 + 0.5f);
	}
}

void thumbnails_update_rect(int layerIndex, IRect canvasRect)
{
	const Layer *layer = &g_document.layers[layerIndex];
	int firstX = canvasRect.x0 / THUMBNAIL_CELL_WIDTH;
	int endX = (canvasRect.x1 + THUMBNAIL_CELL_WIDTH - 1) / THUMBNAIL_CELL_WIDTH;
	int firstY = canvasRect.y0 / THUMBNAIL_CELL_HEIGHT;
	int endY = (canvasRect.y1 + THUMBNAIL_CELL_HEIGHT - 1) / THUMBNAIL_CELL_HEIGHT;
	firstX = firstX < 0 ? 0 : firstX;
	firstY = firstY < 0 ? 0 : firstY;
	endX = endX > LAYER_THUMBNAIL_WIDTH ? LAYER_THUMBNAIL_WIDTH : endX;
	endY = endY > LAYER_THUMBNAIL_HEIGHT ? LAYER_THUMBNAIL_HEIGHT : endY;
	for (int y = firstY; y < endY; y++) {
		for (int x = firstX; x < endX; x++) {
			u8 *pixel = g_thumbnails[layerIndex] + (y * LAYER_THUMBNAIL_WIDTH + x) * RGB_PIXEL_BYTES;
			make_thumbnail_pixel(layer, x, y, pixel);
		}
	}
}

bool thumbnails_update_marked(void)
{
	if (!g_thumbnailsMarked) {
		return false;
	}
	IRect wholePage = {.x0 = 0, .y0 = 0, .x1 = CANVAS_WIDTH, .y1 = CANVAS_HEIGHT};
	for (int i = 0; i < g_document.layerCount; i++) {
		if (g_thumbnailsMarked & (1u << i)) {
			thumbnails_update_rect(i, wholePage);
		}
	}
	g_thumbnailsMarked = 0;
	return true;
}

void thumbnails_draw(Surface *surface, int x, int y, int layerIndex, int alpha)
{
	const u8 *thumbnail = g_thumbnails[layerIndex];
	for (int row = 0; row < LAYER_THUMBNAIL_HEIGHT; row++) {
		for (int column = 0; column < LAYER_THUMBNAIL_WIDTH; column++) {
			const u8 *pixel = thumbnail + (row * LAYER_THUMBNAIL_WIDTH + column) * RGB_PIXEL_BYTES;
			draw_pixel(surface, x + column, y + row, RGB(pixel[0], pixel[1], pixel[2]), alpha);
		}
	}
}
