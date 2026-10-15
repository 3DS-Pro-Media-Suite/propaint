#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "brush_preview.h"
#include "document.h"
#include "math_util.h"
#include "render.h"
#include "textures.h"

#define PREVIEW_LAYER_TYPES 2 // one private layer per layer type
#define PREVIEW_ORIGIN 32 // canvas px from the top left corner where the preview is drawn
#define PREVIEW_WIPE_MARGIN 40 // canvas px around the preview that the stroke may touch
#define PREVIEW_STRENGTH 0.7f // smudge / smooth amount
#define PREVIEW_RADIUS_PART 0.2f // the brush radius as a part of the preview height
#define PREVIEW_STROKE_INSET 24.0f // px of the width the stroke leaves free
#define PREVIEW_STROKE_PART_MIN 0.6f // the shortest stroke as a part of the width
#define PREVIEW_SAMPLE_SPACING 3 // px between two pen samples
#define PREVIEW_SAMPLE_MS 8 // time between two pen samples
#define PREVIEW_WAVE_PART 0.2f // the height of the S curve as a part of the preview height
#define PREVIEW_PRESSURE_MIN 0.12f // the sample stroke's pressure goes from this
#define PREVIEW_PRESSURE_RANGE (1.0f - PREVIEW_PRESSURE_MIN) // up to full
#define PREVIEW_PRESSURE_CURVE 0.7f // exponent: the pressure rises quickly, then levels off
#define PREVIEW_ALPHA_MIN 0.002f // a fainter SDF sample reads as transparent

// Draws the sample stroke: an S curve across the preview, with the pressure rising and then falling.
static void draw_preview_stroke(const Brush *brush, int mode, const u8 color[3], int width, int height)
{
	StrokeSettings settings;
	memset(&settings, 0, sizeof settings);
	settings.mode = (u8)mode;
	settings.color[0] = color[0];
	settings.color[1] = color[1];
	settings.color[2] = color[2];
	settings.opacity = 1;
	settings.strength = PREVIEW_STRENGTH;
	settings.brush = *brush;
	settings.texture = textures_tile(brush->texture);
	settings.size = height * PREVIEW_RADIUS_PART;
	settings.zoom = 1;
	settings.shortField = true; // the private layer has no outline, and the full field costs most of the time
	if (!brush_stroke_begin(&settings)) {
		return;
	}
	float length = fmaxf(width - PREVIEW_STROKE_INSET, width * PREVIEW_STROKE_PART_MIN);
	float margin = (width - length) * 0.5f;
	int sampleCount = (int)(length / PREVIEW_SAMPLE_SPACING);
	if (sampleCount < 1) {
		sampleCount = 1;
	}
	float startX = PREVIEW_ORIGIN + margin;
	float centerY = PREVIEW_ORIGIN + height * 0.5f;
	for (int i = 0; i <= sampleCount; i++) {
		float along = (float)i / sampleCount;
		float x = startX + along * length;
		float y = centerY + sinf(along * TWO_PI) * height * PREVIEW_WAVE_PART;
		float arch = max_float(sinf(along * PI), 0.0f);
		float pressure = PREVIEW_PRESSURE_MIN + PREVIEW_PRESSURE_RANGE * powf(arch, PREVIEW_PRESSURE_CURVE);
		brush_stroke_add(x, y, pressure, (u32)(i * PREVIEW_SAMPLE_MS));
	}
	brush_stroke_end_without_undo();
}

// An SDF layer's pixel as straight RGBA, the way the renderer shows it.
static void read_sdf_pixel(const Layer *layer, int x, int y, u8 *out)
{
	float color[3];
	float alpha;
	render_layer_sample(layer, x, y, color, &alpha);
	if (alpha <= PREVIEW_ALPHA_MIN) {
		memset(out, 0, RGBA_PIXEL_BYTES);
		return;
	}
	render_store_straight_rgba(color, alpha, out);
}

static void read_preview(const Layer *layer, bool isSdf, u8 *rgba, int width, int height)
{
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			u8 *out = rgba + ((size_t)y * width + x) * RGBA_PIXEL_BYTES;
			int canvasX = PREVIEW_ORIGIN + x;
			int canvasY = PREVIEW_ORIGIN + y;
			if (!isSdf) {
				size_t index = (size_t)canvasY * CANVAS_WIDTH + canvasX;
				memcpy(out, layer->pixels + index * RGBA_PIXEL_BYTES, RGBA_PIXEL_BYTES);
				continue;
			}
			read_sdf_pixel(layer, canvasX, canvasY, out);
		}
	}
}

// Clears what the preview stroke may have touched, so the private layer is blank for the next preview.
static void wipe_preview(Layer *layer, int width, int height)
{
	int left = max_int(PREVIEW_ORIGIN - PREVIEW_WIPE_MARGIN, 0);
	int right = min_int(PREVIEW_ORIGIN + width + PREVIEW_WIPE_MARGIN, CANVAS_WIDTH);
	int top = max_int(PREVIEW_ORIGIN - PREVIEW_WIPE_MARGIN, 0);
	int bottom = min_int(PREVIEW_ORIGIN + height + PREVIEW_WIPE_MARGIN, CANVAS_HEIGHT);
	for (int y = top; y < bottom; y++) {
		size_t rowStart = (size_t)y * CANVAS_WIDTH + left;
		memset(layer->pixels + rowStart * RGBA_PIXEL_BYTES, 0, (size_t)(right - left) * RGBA_PIXEL_BYTES);
		if (layer->distance) {
			memset(layer->distance + rowStart, SDF_FAR, (size_t)(right - left));
		}
	}
}

// The preview's private layer of that type, made on first use with that colour. NULL when out of memory.
static Layer *preview_layer(int layerType, const u8 color[3])
{
	static Layer previewLayers[PREVIEW_LAYER_TYPES];
	Layer *layer = &previewLayers[layerType];
	if (layer->pixels) {
		return layer;
	}
	memset(layer, 0, sizeof *layer);
	layer_defaults(&layer->props, layerType, color);
	// Not layer_alloc: when memory is short, a preview is not worth the oldest undo steps.
	layer->id = layer_new_id();
	layer->pixels = calloc(CANVAS_PIXELS, RGBA_PIXEL_BYTES);
	if (layerType == LAYER_SDF) {
		layer->distance = malloc(CANVAS_PIXELS);
		if (layer->distance) {
			memset(layer->distance, SDF_FAR, CANVAS_PIXELS);
		}
	}
	if (!layer->pixels || (layerType == LAYER_SDF && !layer->distance)) {
		layer_free(layer);
		return NULL;
	}
	layer->used = true;
	return layer;
}

// The stroke runs on the private layer standing in for the document.
void brush_preview_draw(const Brush *brush, int mode, const u8 color[3], bool isSdf, u8 *rgba, int width,
                        int height)
{
	bool sizeAllowed = width >= BRUSH_PREVIEW_MIN_SIZE && height >= BRUSH_PREVIEW_MIN_SIZE &&
	                   width <= BRUSH_PREVIEW_MAX_WIDTH && height <= BRUSH_PREVIEW_MAX_HEIGHT;
	Layer *layer = NULL;
	if (!brush_stroke_active() && sizeAllowed) {
		layer = preview_layer(isSdf ? LAYER_SDF : LAYER_PIXEL, color);
	}
	if (!layer) {
		if (width > 0 && height > 0) {
			memset(rgba, 0, (size_t)width * height * RGBA_PIXEL_BYTES);
		}
		return;
	}
	// Static because a Document is too big for the stack.
	static Document savedDocument;
	savedDocument = g_document;
	memset(&g_document, 0, sizeof g_document);
	g_document.layerCount = 1;
	g_document.current = 0;
	g_document.layers[0] = *layer;
	draw_preview_stroke(brush, mode, color, width, height);
	Layer *drawn = &g_document.layers[0];
	read_preview(drawn, isSdf, rgba, width, height);
	wipe_preview(drawn, width, height);
	*layer = *drawn;
	memset(layer->occupancy, 0, sizeof layer->occupancy);
	layer->bounds = (IRect){0, 0, 0, 0};
	g_document = savedDocument;
}
