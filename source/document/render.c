// Compositing: every screen pixel is mapped back to a canvas point and every visible layer is sampled there.
// Pixel layers: nearest pixel. SDF layers: bilinear field sample, then fill / outline / shadow are thresholds
// of it, with an anti-aliasing band exactly one screen pixel wide (1 / zoom canvas px), which is why they
// stay sharp zoomed in. The fill takes its colour and density from the layer's RGBA map, so one SDF layer can
// hold many colours.
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "color_units.h"
#include "framebuffer.h"
#include "math_util.h"
#include "undo.h"

// fast_floor works for |x| below this.
#define FAST_FLOOR_OFFSET 16384
#define FULLY_OPAQUE 255
#define BILINEAR_TAP_COUNT 4
#define RECT_CORNERS 4
// Framebuffer pixels are BGR.
#define FRAMEBUFFER_BLUE 0
#define FRAMEBUFFER_GREEN 1
#define FRAMEBUFFER_RED 2
// The desk around the canvas.
#define DESK_RED 60
#define DESK_GREEN 60
#define DESK_BLUE 66
// Start value of a bounding box search: beyond any screen coordinate.
#define BOUNDING_BOX_START 1e9f

// How far a layer can show past its drawn bounds, in canvas px: a pixel layer one pixel, an SDF layer its
// anti-aliasing band plus its outline, shadow offset and blur, and half its softness.
#define RENDER_PAD_PIXEL_LAYER 1
#define RENDER_PAD_SDF_BAND 3

// floorf is a library call on the ARM11; this is a plain conversion.
static inline int fast_floor(float x)
{
	return (int)(x + (float)FAST_FLOOR_OFFSET) - FAST_FLOOR_OFFSET;
}

static inline u8 unit_to_byte(float value)
{
	return (u8)(clamp_unit(value) * UNIT_TO_BYTE + 0.5f);
}

static inline float field_at(const s8 *field, int x, int y)
{
	if ((unsigned)x >= CANVAS_WIDTH || (unsigned)y >= CANVAS_HEIGHT) {
		return SDF_FAR;
	}
	return field[y * CANVAS_WIDTH + x];
}

// Field value at canvas point (u, v) in canvas px (pixel centres sit at +0.5).
static inline float sample_field(const s8 *field, float u, float v)
{
	float pixelU = u - 0.5f;
	float pixelV = v - 0.5f;
	int x0 = fast_floor(pixelU);
	int y0 = fast_floor(pixelV);
	float fractionX = pixelU - x0;
	float fractionY = pixelV - y0;
	float topLeft;
	float topRight;
	float bottomLeft;
	float bottomRight;
	// The unsigned compare also rejects negative coordinates.
	if ((unsigned)x0 < CANVAS_WIDTH - 1 && (unsigned)y0 < CANVAS_HEIGHT - 1) {
		const s8 *topLeftValue = field + y0 * CANVAS_WIDTH + x0;
		topLeft = topLeftValue[0];
		topRight = topLeftValue[1];
		bottomLeft = topLeftValue[CANVAS_WIDTH];
		bottomRight = topLeftValue[CANVAS_WIDTH + 1];
	} else {
		topLeft = field_at(field, x0, y0);
		topRight = field_at(field, x0 + 1, y0);
		bottomLeft = field_at(field, x0, y0 + 1);
		bottomRight = field_at(field, x0 + 1, y0 + 1);
	}
	float top = topLeft + (topRight - topLeft) * fractionX;
	float bottom = bottomLeft + (bottomRight - bottomLeft) * fractionX;
	return (top + (bottom - top) * fractionY) * (1.0f / SDF_UNITS_PER_PIXEL);
}

float render_field_bilinear(const s8 *field, float u, float v)
{
	return sample_field(field, u, v);
}

void render_store_straight_rgba(const float color[3], float alpha, u8 out[4])
{
	for (int c = 0; c < RGB_CHANNELS; c++) {
		float value = color[c] / alpha * UNIT_TO_BYTE + 0.5f;
		out[c] = value > 255 ? 255 : (u8)value;
	}
	out[RGBA_ALPHA_OFFSET] = (u8)(alpha * UNIT_TO_BYTE + 0.5f);
}

// The four pixels of an RGBA map around a canvas point and their bilinear weights. Edge pixels repeat.
typedef struct {
	const u8 *pixels[BILINEAR_TAP_COUNT]; // (x0, y0), (x0 + 1, y0), (x0, y0 + 1), (x0 + 1, y0 + 1)
	float weights[BILINEAR_TAP_COUNT];
} BilinearTaps;

static inline const u8 *rgba_pixel(const u8 *pixels, int x, int y)
{
	return pixels + ((size_t)y * CANVAS_WIDTH + x) * RGBA_PIXEL_BYTES;
}

static inline void bilinear_taps(const u8 *pixels, float u, float v, BilinearTaps *taps)
{
	float pixelU = u - 0.5f;
	float pixelV = v - 0.5f;
	int x0 = fast_floor(pixelU);
	int y0 = fast_floor(pixelV);
	float fractionX = pixelU - x0;
	float fractionY = pixelV - y0;
	int left = clamp_int(x0, 0, CANVAS_WIDTH - 1);
	int right = clamp_int(x0 + 1, 0, CANVAS_WIDTH - 1);
	int top = clamp_int(y0, 0, CANVAS_HEIGHT - 1);
	int bottom = clamp_int(y0 + 1, 0, CANVAS_HEIGHT - 1);
	taps->pixels[0] = rgba_pixel(pixels, left, top);
	taps->pixels[1] = rgba_pixel(pixels, right, top);
	taps->pixels[2] = rgba_pixel(pixels, left, bottom);
	taps->pixels[3] = rgba_pixel(pixels, right, bottom);
	taps->weights[0] = (1 - fractionX) * (1 - fractionY);
	taps->weights[1] = fractionX * (1 - fractionY);
	taps->weights[2] = (1 - fractionX) * fractionY;
	taps->weights[3] = fractionX * fractionY;
}

void render_pixels_bilinear(const u8 *pixels, float u, float v, float out[4])
{
	BilinearTaps taps;
	bilinear_taps(pixels, u, v, &taps);
	out[0] = 0;
	out[1] = 0;
	out[2] = 0;
	out[3] = 0;
	for (int i = 0; i < BILINEAR_TAP_COUNT; i++) {
		const u8 *pixel = taps.pixels[i];
		float alpha = pixel[RGBA_ALPHA_OFFSET] * BYTE_TO_UNIT;
		float weightedAlpha = taps.weights[i] * alpha;
		out[0] += pixel[0] * weightedAlpha;
		out[1] += pixel[1] * weightedAlpha;
		out[2] += pixel[2] * weightedAlpha;
		out[3] += taps.weights[i] * pixel[RGBA_ALPHA_OFFSET];
	}
}

// An SDF layer's fill colour and density at a point. Transparent neighbours do not count, so the colour and
// the density stay right up to the edge instead of fading before it.
static void sdf_fill_color(const u8 *pixels, float u, float v, float rgb[3], float *density)
{
	BilinearTaps taps;
	bilinear_taps(pixels, u, v, &taps);
	const u8 *const *tap = taps.pixels;
	const float *weight = taps.weights;
	int sharedAlphaBits = tap[0][RGBA_ALPHA_OFFSET] & tap[1][RGBA_ALPHA_OFFSET] & tap[2][RGBA_ALPHA_OFFSET] &
	                      tap[3][RGBA_ALPHA_OFFSET];
	if (sharedAlphaBits == FULLY_OPAQUE) {
		// All four opaque: a plain bilinear colour, no divisions.
		for (int c = 0; c < 3; c++) {
			rgb[c] = tap[0][c] * weight[0] + tap[1][c] * weight[1] + tap[2][c] * weight[2] +
			         tap[3][c] * weight[3];
		}
		*density = 1;
		return;
	}
	float weightSum = 0;
	float alphaSum = 0;
	float redSum = 0;
	float greenSum = 0;
	float blueSum = 0;
	for (int i = 0; i < BILINEAR_TAP_COUNT; i++) {
		if (!tap[i][RGBA_ALPHA_OFFSET]) {
			continue;
		}
		float weightedAlpha = weight[i] * tap[i][RGBA_ALPHA_OFFSET];
		weightSum += weight[i];
		alphaSum += weightedAlpha;
		redSum += tap[i][0] * weightedAlpha;
		greenSum += tap[i][1] * weightedAlpha;
		blueSum += tap[i][2] * weightedAlpha;
	}
	if (alphaSum <= 0) {
		rgb[0] = 0;
		rgb[1] = 0;
		rgb[2] = 0;
		*density = 0;
		return;
	}
	float inverseAlphaSum = 1.0f / alphaSum;
	rgb[0] = redSum * inverseAlphaSum;
	rgb[1] = greenSum * inverseAlphaSum;
	rgb[2] = blueSum * inverseAlphaSum;
	*density = alphaSum / (weightSum * UNIT_TO_BYTE);
}

// Paints a colour (0..255) with a coverage over a premultiplied accumulator (colour 0..255 x alpha).
static inline void paint_over(float color[3], float *alpha, float red, float green, float blue,
                              float coverage)
{
	if (coverage <= 0) {
		return;
	}
	float remaining = 1.0f - coverage;
	color[0] = color[0] * remaining + red * coverage;
	color[1] = color[1] * remaining + green * coverage;
	color[2] = color[2] * remaining + blue * coverage;
	*alpha = *alpha * remaining + coverage;
}

static inline void paint_bytes_over(float color[3], float *alpha, const u8 *paint, float coverage)
{
	paint_over(color, alpha, paint[0], paint[1], paint[2], coverage);
}

// inverseBand is 1 / (width of the anti-aliasing band in canvas px), inverseShadowBand the same for the
// shadow (band + blur). Coverage is one half on an edge and ramps over one band width.
static void shade_sdf_layer(const Layer *layer, float u, float v, float inverseBand, float inverseShadowBand,
                            float color[3], float *alpha)
{
	const LayerProps *props = &layer->props;
	float distance = sample_field(layer->distance, u, v);
	if (props->shadowOn) {
		float shadowDistance = sample_field(layer->distance, u - props->shadowX, v - props->shadowY);
		float shadowCoverage = clamp_unit(0.5f - shadowDistance * inverseShadowBand);
		float shadowOpacity = props->shadowAlpha * BYTE_TO_UNIT;
		paint_bytes_over(color, alpha, props->shadowColor, shadowCoverage * shadowOpacity);
	}
	if (props->outlineWidth) {
		float outlineCoverage = clamp_unit(0.5f - (distance - props->outlineWidth) * inverseBand);
		paint_bytes_over(color, alpha, props->outlineColor, outlineCoverage);
	}
	float fillCoverage = 0.5f - distance * inverseBand;
	if (fillCoverage > 0) {
		if (fillCoverage > 1) {
			fillCoverage = 1;
		}
		float rgb[3];
		float density;
		sdf_fill_color(layer->pixels, u, v, rgb, &density);
		paint_over(color, alpha, rgb[0], rgb[1], rgb[2], fillCoverage * density);
	}
}

static inline void shade_pixel_layer(const Layer *layer, float u, float v, float color[3], float *alpha)
{
	const u8 *pixel = rgba_pixel(layer->pixels, (int)u, (int)v);
	float pixelAlpha = pixel[RGBA_ALPHA_OFFSET] * BYTE_TO_UNIT;
	*alpha = pixelAlpha;
	// The same units as paint_over: colour 0..255 x alpha.
	color[0] = pixel[0] * pixelAlpha;
	color[1] = pixel[1] * pixelAlpha;
	color[2] = pixel[2] * pixelAlpha;
}

// The anti-aliasing band in canvas px: one screen pixel, or the layer's softness when that is wider.
static inline float band_width(const LayerProps *props, float zoom)
{
	float screenPixel = 1.0f / zoom;
	float softWidth = props->softness * 0.5f; // softness is in half pixels
	return softWidth > screenPixel ? softWidth : screenPixel;
}

void render_layer_sample(const Layer *layer, int x, int y, float color[3], float *alpha)
{
	color[0] = 0;
	color[1] = 0;
	color[2] = 0;
	*alpha = 0;
	float u = x + 0.5f;
	float v = y + 0.5f;
	if (layer->props.type == LAYER_PIXEL) {
		if (layer->pixels) {
			shade_pixel_layer(layer, u, v, color, alpha);
		}
	} else if (layer->distance && layer->pixels) {
		float band = band_width(&layer->props, 1.0f);
		shade_sdf_layer(layer, u, v, 1.0f / band, 1.0f / (band + layer->props.shadowBlur), color, alpha);
	}
	// The accumulator holds colour 0..255 x alpha; callers want colour 0..1 x alpha.
	color[0] *= BYTE_TO_UNIT;
	color[1] *= BYTE_TO_UNIT;
	color[2] *= BYTE_TO_UNIT;
}

// Compositing with clipping masks and groups. Colours here are premultiplied 0..1, and so is the backdrop.
// The page is an opaque backdrop (alpha 1); an isolated group starts from nothing (alpha 0).

static inline void blend_onto(float backdrop[3], float *backdropAlpha, const float source[3],
                              float sourceAlpha, int blendMode)
{
	if (sourceAlpha <= 0) {
		return;
	}
	float oldAlpha = *backdropAlpha;
	switch (blendMode) {
	case BLEND_MULTIPLY:
		// Multiply in premultiplied terms: (1 - as) * Cb + (1 - ab) * Cs + Cb * Cs.
		for (int c = 0; c < 3; c++) {
			backdrop[c] =
			        backdrop[c] * (1 - sourceAlpha) + source[c] * (1 - oldAlpha) + backdrop[c] * source[c];
		}
		break;
	case BLEND_ADD:
		for (int c = 0; c < 3; c++) {
			backdrop[c] += source[c];
		}
		break;
	default:
		for (int c = 0; c < 3; c++) {
			backdrop[c] = source[c] + backdrop[c] * (1 - sourceAlpha);
		}
		break;
	}
	*backdropAlpha = sourceAlpha + oldAlpha * (1 - sourceAlpha);
}

// Per layer, for the zoom of the last prepare_layers call.
static IRect g_paddedBounds[MAX_LAYERS]; // content bounds, padded for outline, shadow and softness
static u8 g_layerActive[MAX_LAYERS]; // visible, used, has data and has content
static float g_inverseBand[MAX_LAYERS]; // 1 / anti-aliasing band width
static float g_inverseShadowBand[MAX_LAYERS]; // 1 / (band width + shadow blur)

static bool layer_can_contribute(const Layer *layer)
{
	const LayerProps *props = &layer->props;
	if (!props->visible || !props->opacity || !layer->used || !layer->pixels) {
		return false;
	}
	if (props->type != LAYER_PIXEL && !layer->distance) {
		return false;
	}
	return layer->bounds.x1 > layer->bounds.x0 && layer->bounds.y1 > layer->bounds.y0;
}

int render_layer_padding(const LayerProps *props)
{
	if (props->type != LAYER_SDF) {
		return RENDER_PAD_PIXEL_LAYER;
	}
	int shadowReach = 0;
	if (props->shadowOn) {
		int offsetX = abs(props->shadowX);
		int offsetY = abs(props->shadowY);
		int largerOffset = offsetX > offsetY ? offsetX : offsetY;
		shadowReach = props->shadowBlur + largerOffset;
	}
	return RENDER_PAD_SDF_BAND + props->outlineWidth + shadowReach + props->softness / 2;
}

static void prepare_layers(float zoom)
{
	for (int i = 0; i < g_document.layerCount; i++) {
		const Layer *layer = &g_document.layers[i];
		g_layerActive[i] = layer_can_contribute(layer);
		int padding = render_layer_padding(&layer->props);
		g_paddedBounds[i].x0 = layer->bounds.x0 - padding;
		g_paddedBounds[i].y0 = layer->bounds.y0 - padding;
		g_paddedBounds[i].x1 = layer->bounds.x1 + padding;
		g_paddedBounds[i].y1 = layer->bounds.y1 + padding;
		float band = band_width(&layer->props, zoom);
		g_inverseBand[i] = 1.0f / band;
		g_inverseShadowBand[i] = 1.0f / (band + layer->props.shadowBlur);
	}
}

void render_prep(void)
{
	prepare_layers(1.0f);
}

IRect render_pad_on_page(IRect rect, int padding)
{
	IRect padded = {
	        .x0 = max_int(rect.x0 - padding, 0),
	        .y0 = max_int(rect.y0 - padding, 0),
	        .x1 = min_int(rect.x1 + padding, CANVAS_WIDTH),
	        .y1 = min_int(rect.y1 + padding, CANVAS_HEIGHT),
	};
	return padded;
}

// Inside the layer's padded bounds and in an occupancy cell that may hold something.
static inline bool layer_may_cover(int index, float u, float v)
{
	const IRect *bounds = &g_paddedBounds[index];
	if (!g_layerActive[index]) {
		return false;
	}
	bool insideBounds = u >= bounds->x0 && u < bounds->x1 && v >= bounds->y0 && v < bounds->y1;
	if (!insideBounds) {
		return false;
	}
	int cellX = (int)u >> OCCUPANCY_CELL_SHIFT;
	int cellY = (int)v >> OCCUPANCY_CELL_SHIFT;
	return g_document.layers[index].occupancy[cellY * OCCUPANCY_WIDTH + cellX];
}

// One layer onto the backdrop. *baseAlpha is the alpha of the current base layer: a clipped layer only shows
// where it is non-zero, and any other layer becomes the new base.
static void composite_layer(int index, float *baseAlpha, float u, float v, float backdrop[3],
                            float *backdropAlpha)
{
	const Layer *layer = &g_document.layers[index];
	bool clipped = layer->props.clipped;
	if (!layer_may_cover(index, u, v)) {
		if (!clipped) {
			*baseAlpha = 0; // its clipped layers vanish too
		}
		return;
	}
	float color[3] = {0, 0, 0};
	float alpha = 0;
	if (layer->props.type == LAYER_PIXEL) {
		shade_pixel_layer(layer, u, v, color, &alpha);
	} else {
		shade_sdf_layer(layer, u, v, g_inverseBand[index], g_inverseShadowBand[index], color, &alpha);
	}
	float opacity = layer->props.opacity * PERCENT_TO_UNIT;
	float layerAlpha = alpha * opacity;
	if (!clipped) {
		*baseAlpha = layerAlpha;
	}
	float clipAlpha = clipped ? *baseAlpha : 1.0f;
	if (layerAlpha <= 0 || clipAlpha <= 0) {
		return;
	}
	float scale = opacity * clipAlpha * BYTE_TO_UNIT;
	float source[3] = {color[0] * scale, color[1] * scale, color[2] * scale};
	blend_onto(backdrop, backdropAlpha, source, layerAlpha * clipAlpha, layer->props.blend);
}

// Layers bottom..top of one container (no nested groups).
static void composite_layer_range(int bottom, int top, float u, float v, float backdrop[3],
                                  float *backdropAlpha)
{
	float baseAlpha = 0;
	for (int i = bottom; i <= top; i++) {
		composite_layer(i, &baseAlpha, u, v, backdrop, backdropAlpha);
	}
}

// A group's layers composited on their own, then onto the backdrop with the group's opacity and blend mode.
// Returns the group's alpha, the base for the layers clipped to it.
static float composite_group(int groupId, int bottom, int top, float u, float v, float backdrop[3],
                             float *backdropAlpha)
{
	const Group *group = &g_document.groups[groupId];
	if (!group->visible || !group->opacity) {
		return 0;
	}
	float groupColor[3] = {0, 0, 0};
	float groupAlpha = 0;
	float opacity = group->opacity * PERCENT_TO_UNIT;
	composite_layer_range(bottom, top, u, v, groupColor, &groupAlpha);
	float source[3] = {groupColor[0] * opacity, groupColor[1] * opacity, groupColor[2] * opacity};
	blend_onto(backdrop, backdropAlpha, source, groupAlpha * opacity, group->blend);
	return groupAlpha * opacity;
}

// The whole document: root layers one by one, every group as a unit.
static void composite_document(float u, float v, float backdrop[3], float *backdropAlpha)
{
	float baseAlpha = 0;
	int index = 0;
	while (index < g_document.layerCount) {
		int groupId = g_document.layers[index].props.group;
		if (!groupId) {
			composite_layer(index, &baseAlpha, u, v, backdrop, backdropAlpha);
			index++;
			continue;
		}
		int groupTop = index;
		while (groupTop + 1 < g_document.layerCount &&
		       g_document.layers[groupTop + 1].props.group == groupId) {
			groupTop++;
		}
		baseAlpha = composite_group(groupId, index, groupTop, u, v, backdrop, backdropAlpha);
		index = groupTop + 1;
	}
}

void render_group_sample(int bottom, int top, int x, int y, float color[3], float *alpha)
{
	color[0] = 0;
	color[1] = 0;
	color[2] = 0;
	*alpha = 0;
	composite_layer_range(bottom, top, x + 0.5f, y + 0.5f, color, alpha);
}

void render_screen_to_canvas(const View *view, int width, int height, float screenX, float screenY, float *u,
                             float *v)
{
	float dx = screenX - width * 0.5f;
	float dy = screenY - height * 0.5f;
	float cosine = cosf(view->rotation);
	float sine = sinf(view->rotation);
	float rotatedX = dx * cosine + dy * sine;
	float rotatedY = -dx * sine + dy * cosine;
	if (view->mirrored) {
		rotatedX = -rotatedX;
	}
	*u = view->centerX + rotatedX / view->zoom;
	*v = view->centerY + rotatedY / view->zoom;
}

static void canvas_to_screen(const View *view, int width, int height, float u, float v, float *screenX,
                             float *screenY)
{
	float scaledX = (u - view->centerX) * view->zoom;
	float scaledY = (v - view->centerY) * view->zoom;
	float cosine = cosf(view->rotation);
	float sine = sinf(view->rotation);
	if (view->mirrored) {
		scaledX = -scaledX;
	}
	*screenX = width * 0.5f + scaledX * cosine - scaledY * sine;
	*screenY = height * 0.5f + scaledX * sine + scaledY * cosine;
}

IRect render_canvas_rect_on_screen(const View *view, int width, int height, IRect canvasRect)
{
	float cornerX[RECT_CORNERS] = {(float)canvasRect.x0, (float)canvasRect.x1, (float)canvasRect.x1,
	                               (float)canvasRect.x0};
	float cornerY[RECT_CORNERS] = {(float)canvasRect.y0, (float)canvasRect.y0, (float)canvasRect.y1,
	                               (float)canvasRect.y1};
	float minX = BOUNDING_BOX_START;
	float minY = BOUNDING_BOX_START;
	float maxX = -BOUNDING_BOX_START;
	float maxY = -BOUNDING_BOX_START;
	for (int i = 0; i < RECT_CORNERS; i++) {
		float screenX;
		float screenY;
		canvas_to_screen(view, width, height, cornerX[i], cornerY[i], &screenX, &screenY);
		if (screenX < minX) {
			minX = screenX;
		}
		if (screenX > maxX) {
			maxX = screenX;
		}
		if (screenY < minY) {
			minY = screenY;
		}
		if (screenY > maxY) {
			maxY = screenY;
		}
	}
	IRect screenRect = {(int)floorf(minX) - 1, (int)floorf(minY) - 1, (int)ceilf(maxX) + 1,
	                    (int)ceilf(maxY) + 1};
	if (screenRect.x0 < 0) {
		screenRect.x0 = 0;
	}
	if (screenRect.y0 < 0) {
		screenRect.y0 = 0;
	}
	if (screenRect.x1 > width) {
		screenRect.x1 = width;
	}
	if (screenRect.y1 > height) {
		screenRect.y1 = height;
	}
	return screenRect;
}

// The screen-to-canvas mapping of a view, with the zoom folded into the rotation.
typedef struct {
	float cosine; // cos(rotation) / zoom
	float sine; // sin(rotation) / zoom
	float mirror; // -1 when the view is mirrored, else 1
} ScreenToCanvas;

static inline ScreenToCanvas screen_to_canvas_mapping(const View *view)
{
	float inverseZoom = 1.0f / view->zoom;
	ScreenToCanvas mapping;
	mapping.cosine = cosf(view->rotation) * inverseZoom;
	mapping.sine = sinf(view->rotation) * inverseZoom;
	mapping.mirror = view->mirrored ? -1 : 1;
	return mapping;
}

// (dx, dy) is a screen offset from the screen centre.
static inline void map_screen_offset(const View *view, const ScreenToCanvas *mapping, float dx, float dy,
                                     float *u, float *v)
{
	*u = view->centerX + mapping->mirror * (dx * mapping->cosine + dy * mapping->sine);
	*v = view->centerY + (-dx * mapping->sine + dy * mapping->cosine);
}

static inline void store_desk_pixel(u8 *pixel)
{
	pixel[FRAMEBUFFER_BLUE] = DESK_BLUE;
	pixel[FRAMEBUFFER_GREEN] = DESK_GREEN;
	pixel[FRAMEBUFFER_RED] = DESK_RED;
}

// The document at a canvas point on white paper, as a framebuffer pixel.
static inline void store_document_pixel(u8 *pixel, float u, float v)
{
	float color[3] = {1, 1, 1};
	float alpha = 1;
	composite_document(u, v, color, &alpha);
	pixel[FRAMEBUFFER_BLUE] = unit_to_byte(color[2]);
	pixel[FRAMEBUFFER_GREEN] = unit_to_byte(color[1]);
	pixel[FRAMEBUFFER_RED] = unit_to_byte(color[0]);
}

static void render_area(const View *view, u8 *destination, int width, int height, int x0, int y0, int x1,
                        int y1)
{
	if (x0 < 0) {
		x0 = 0;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (x1 > width) {
		x1 = width;
	}
	if (y1 > height) {
		y1 = height;
	}
	ScreenToCanvas mapping = screen_to_canvas_mapping(view);
	// The canvas step for one screen row down a column.
	float stepU = mapping.mirror * mapping.sine;
	float stepV = mapping.cosine;
	for (int x = x0; x < x1; x++) {
		float dx = x + 0.5f - width * 0.5f;
		float dy = y0 + 0.5f - height * 0.5f;
		float u;
		float v;
		map_screen_offset(view, &mapping, dx, dy, &u, &v);
		u8 *column = destination + (size_t)x * height * FRAMEBUFFER_PIXEL_BYTES;
		for (int y = y0; y < y1; y++) {
			u8 *pixel = column + (height - 1 - y) * FRAMEBUFFER_PIXEL_BYTES;
			bool outsideCanvas = u < 0 || u >= CANVAS_WIDTH || v < 0 || v >= CANVAS_HEIGHT;
			if (outsideCanvas) {
				store_desk_pixel(pixel);
			} else {
				store_document_pixel(pixel, u, v);
			}
			u += stepU;
			v += stepV;
		}
	}
}

void render(const View *view, u8 *destination, int width, int height, int x0, int y0, int x1, int y1)
{
	prepare_layers(view->zoom);
	render_area(view, destination, width, height, x0, y0, x1, y1);
}

// Fills a block of a framebuffer-shaped image with one pixel.
static void fill_block(u8 *destination, int height, IRect block, const u8 *pixel)
{
	for (int x = block.x0; x < block.x1; x++) {
		u8 *target = destination + ((size_t)x * height + (height - 1 - block.y0)) * FRAMEBUFFER_PIXEL_BYTES;
		for (int y = block.y0; y < block.y1; y++) {
			target[0] = pixel[0];
			target[1] = pixel[1];
			target[2] = pixel[2];
			target -= FRAMEBUFFER_PIXEL_BYTES; // the next row down is stored before this one
		}
	}
}

void render_lowres(const View *view, u8 *destination, int width, int height, int step)
{
	if (step < 1) {
		step = 1;
	}
	if (step == 1) {
		render(view, destination, width, height, 0, 0, width, height);
		return;
	}
	prepare_layers(view->zoom);
	ScreenToCanvas mapping = screen_to_canvas_mapping(view);
	for (int x = 0; x < width; x += step) {
		int blockRight = x + step < width ? x + step : width;
		float dx = (x + blockRight) * 0.5f - width * 0.5f;
		for (int y = 0; y < height; y += step) {
			int blockBottom = y + step < height ? y + step : height;
			float dy = (y + blockBottom) * 0.5f - height * 0.5f;
			float u;
			float v;
			map_screen_offset(view, &mapping, dx, dy, &u, &v);
			u8 sample[FRAMEBUFFER_PIXEL_BYTES];
			bool insideCanvas = u >= 0 && u < CANVAS_WIDTH && v >= 0 && v < CANVAS_HEIGHT;
			if (insideCanvas) {
				store_document_pixel(sample, u, v);
			} else {
				store_desk_pixel(sample);
			}
			IRect block = {x, y, blockRight, blockBottom};
			fill_block(destination, height, block, sample);
		}
	}
}

u8 *render_flattened(int width, int height)
{
	u8 *framebuffer = undo_malloc((size_t)width * height * FRAMEBUFFER_PIXEL_BYTES);
	u8 *rgb = undo_malloc((size_t)width * height * RGB_PIXEL_BYTES);
	if (!framebuffer || !rgb) {
		free(framebuffer);
		free(rgb);
		return NULL;
	}
	View view = {
	        .centerX = CANVAS_WIDTH * 0.5f,
	        .centerY = CANVAS_HEIGHT * 0.5f,
	        .zoom = (float)width / CANVAS_WIDTH,
	        .rotation = 0,
	        .mirrored = 0,
	};
	render(&view, framebuffer, width, height, 0, 0, width, height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const u8 *source =
			        framebuffer + ((size_t)x * height + (height - 1 - y)) * FRAMEBUFFER_PIXEL_BYTES;
			u8 *target = rgb + ((size_t)y * width + x) * RGB_PIXEL_BYTES;
			target[0] = source[FRAMEBUFFER_RED];
			target[1] = source[FRAMEBUFFER_GREEN];
			target[2] = source[FRAMEBUFFER_BLUE];
		}
	}
	free(framebuffer);
	return rgb;
}
