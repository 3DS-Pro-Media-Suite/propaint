#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "layer_convert.h"
#include "color_units.h"
#include "document.h"
#include "layer_stack.h"
#include "render.h"
#include "undo.h"

#define MIN_STORED_ALPHA 0.002f // coverage at or below this is stored as transparent

#define SHAPE_ALPHA_THRESHOLD 128 // pixels with at least this alpha become the shape
#define DISTANCE_UNREACHED 1e9f // larger than any distance on the canvas
#define EDGE_OFFSET 0.5f // the edge lies halfway between an inside and an outside pixel
// Chamfer distances to a straight and to a diagonal neighbour.
#define STRAIGHT_STEP 1.0f
#define DIAGONAL_STEP 1.41421356f // sqrt(2)

static u8 *canvas_pixel(u8 *pixels, int x, int y)
{
	return pixels + ((size_t)y * CANVAS_WIDTH + x) * RGBA_PIXEL_BYTES;
}

// The layer as it looks at zoom 1, as a new pixel map. NULL when out of memory.
static u8 *render_layer_pixels(const Layer *layer)
{
	u8 *pixels = undo_malloc(CANVAS_PIXELS * RGBA_PIXEL_BYTES);
	if (!pixels) {
		return NULL;
	}
	memset(pixels, 0, CANVAS_PIXELS * RGBA_PIXEL_BYTES);
	for (int y = 0; y < CANVAS_HEIGHT; y++) {
		for (int x = 0; x < CANVAS_WIDTH; x++) {
			float color[RGB_CHANNELS];
			float alpha;
			render_layer_sample(layer, x, y, color, &alpha);
			if (alpha <= MIN_STORED_ALPHA) {
				continue;
			}
			render_store_straight_rgba(color, alpha, canvas_pixel(pixels, x, y));
		}
	}
	return pixels;
}

bool layer_to_pixel(int layerIndex)
{
	if (layerIndex < 0 || layerIndex >= g_document.layerCount) {
		return false;
	}
	Layer *layer = &g_document.layers[layerIndex];
	if (layer->props.type == LAYER_PIXEL) {
		return true;
	}
	u8 *pixels = render_layer_pixels(layer);
	if (!pixels) {
		return false;
	}
	undo_forget_layer(layer->id); // its undo steps hold the other layer type
	free(layer->pixels);
	free(layer->distance);
	layer->pixels = pixels;
	layer->distance = NULL;
	layer->props.type = LAYER_PIXEL;
	layer->props.outlineWidth = 0;
	layer->props.shadowOn = 0;
	layer->used = true;
	layer_scan_bounds(layer);
	return true;
}

static float shorter_distance(float distance, float neighbourDistance, float step)
{
	float throughNeighbour = neighbourDistance + step;
	return throughNeighbour < distance ? throughNeighbour : distance;
}

// 3x3 chamfer distance transform. The field is 0 on the target pixels and DISTANCE_UNREACHED elsewhere;
// afterwards it holds each pixel's distance to the nearest target pixel.
static void chamfer_distance(float *field)
{
	for (int y = 0; y < CANVAS_HEIGHT; y++) {
		for (int x = 0; x < CANVAS_WIDTH; x++) {
			float *cell = field + (size_t)y * CANVAS_WIDTH + x;
			float distance = *cell;
			if (x > 0) {
				distance = shorter_distance(distance, cell[-1], STRAIGHT_STEP);
			}
			if (y > 0) {
				const float *rowAbove = cell - CANVAS_WIDTH;
				distance = shorter_distance(distance, rowAbove[0], STRAIGHT_STEP);
				if (x > 0) {
					distance = shorter_distance(distance, rowAbove[-1], DIAGONAL_STEP);
				}
				if (x < CANVAS_WIDTH - 1) {
					distance = shorter_distance(distance, rowAbove[1], DIAGONAL_STEP);
				}
			}
			*cell = distance;
		}
	}
	for (int y = CANVAS_HEIGHT - 1; y >= 0; y--) {
		for (int x = CANVAS_WIDTH - 1; x >= 0; x--) {
			float *cell = field + (size_t)y * CANVAS_WIDTH + x;
			float distance = *cell;
			if (x < CANVAS_WIDTH - 1) {
				distance = shorter_distance(distance, cell[1], STRAIGHT_STEP);
			}
			if (y < CANVAS_HEIGHT - 1) {
				const float *rowBelow = cell + CANVAS_WIDTH;
				distance = shorter_distance(distance, rowBelow[0], STRAIGHT_STEP);
				if (x < CANVAS_WIDTH - 1) {
					distance = shorter_distance(distance, rowBelow[1], DIAGONAL_STEP);
				}
				if (x > 0) {
					distance = shorter_distance(distance, rowBelow[-1], DIAGONAL_STEP);
				}
			}
			*cell = distance;
		}
	}
}

static bool is_shape_pixel(const u8 *pixels, size_t index)
{
	return pixels[index * RGBA_PIXEL_BYTES + RGBA_ALPHA_OFFSET] >= SHAPE_ALPHA_THRESHOLD;
}

static s8 clamp_to_field(float value)
{
	if (value < -SDF_FAR) {
		return -SDF_FAR;
	}
	if (value > SDF_FAR) {
		return SDF_FAR;
	}
	return (s8)value;
}

bool layer_to_sdf(int layerIndex)
{
	if (layerIndex < 0 || layerIndex >= g_document.layerCount) {
		return false;
	}
	Layer *layer = &g_document.layers[layerIndex];
	if (layer->props.type == LAYER_SDF) {
		return true;
	}
	// Inside pixels measure to the nearest outside pixel, outside pixels to the nearest inside pixel.
	float *distanceToOutside = undo_malloc(CANVAS_PIXELS * sizeof(float));
	float *distanceToInside = undo_malloc(CANVAS_PIXELS * sizeof(float));
	s8 *field = undo_malloc(CANVAS_PIXELS);
	if (!distanceToOutside || !distanceToInside || !field) {
		free(distanceToOutside);
		free(distanceToInside);
		free(field);
		return false;
	}
	bool hasShape = false;
	for (size_t i = 0; i < CANVAS_PIXELS; i++) {
		bool inside = is_shape_pixel(layer->pixels, i);
		distanceToOutside[i] = inside ? DISTANCE_UNREACHED : 0;
		distanceToInside[i] = inside ? 0 : DISTANCE_UNREACHED;
		hasShape |= inside;
	}
	chamfer_distance(distanceToOutside);
	chamfer_distance(distanceToInside);
	for (size_t i = 0; i < CANVAS_PIXELS; i++) {
		float edgeDistance;
		if (is_shape_pixel(layer->pixels, i)) {
			edgeDistance = -(distanceToOutside[i] - EDGE_OFFSET);
		} else {
			edgeDistance = distanceToInside[i] - EDGE_OFFSET;
		}
		field[i] = clamp_to_field(edgeDistance * SDF_UNITS_PER_PIXEL);
	}
	free(distanceToOutside);
	free(distanceToInside);
	undo_forget_layer(layer->id); // its undo steps hold the other layer type
	layer->distance = field;
	layer->props.type = LAYER_SDF;
	layer->used = hasShape;
	layer_scan_bounds(layer);
	return true;
}

// Only Normal blend onto a visible lower layer gives the same result as on screen. Both layers must be in the
// same container, and a normal layer cannot go into a clipped one because the mask would be lost.
static int merge_down_check(const Layer *lower, const Layer *upper)
{
	if (lower->props.blend != BLEND_NORMAL || upper->props.blend != BLEND_NORMAL) {
		return MERGE_NEEDS_NORMAL_BLEND;
	}
	if (!lower->props.visible) {
		return MERGE_BELOW_HIDDEN;
	}
	if (lower->props.group != upper->props.group) {
		return MERGE_OTHER_CONTAINER;
	}
	if (lower->props.clipped && !upper->props.clipped) {
		return MERGE_OTHER_CONTAINER;
	}
	return MERGE_DONE;
}

// Premultiplied colour and coverage of a layer at a canvas pixel, scaled by the layer's opacity.
static void sample_with_opacity(const Layer *layer, int x, int y, float opacity, float color[3], float *alpha)
{
	render_layer_sample(layer, x, y, color, alpha);
	*alpha *= opacity;
	for (int c = 0; c < RGB_CHANNELS; c++) {
		color[c] *= opacity;
	}
}

int layer_merge_down(int layerIndex)
{
	if (layerIndex < 1 || layerIndex >= g_document.layerCount) {
		return MERGE_NO_LAYER_BELOW;
	}
	Layer *lower = &g_document.layers[layerIndex - 1];
	Layer *upper = &g_document.layers[layerIndex];
	int check = merge_down_check(lower, upper);
	if (check != MERGE_DONE) {
		return check;
	}
	// A clipped layer merged into its base only shows where the base has alpha.
	bool clipUpper = upper->props.clipped && !lower->props.clipped;
	if (!layer_to_pixel(layerIndex - 1)) {
		return MERGE_NO_MEMORY;
	}
	undo_forget_layer(lower->id); // the merge rewrites it outside the undo steps
	float lowerOpacity = lower->props.opacity * PERCENT_TO_UNIT;
	float upperOpacity = upper->props.opacity * PERCENT_TO_UNIT;
	for (int y = 0; y < CANVAS_HEIGHT; y++) {
		for (int x = 0; x < CANVAS_WIDTH; x++) {
			float lowerColor[RGB_CHANNELS];
			float lowerAlpha;
			sample_with_opacity(lower, x, y, lowerOpacity, lowerColor, &lowerAlpha);
			float upperColor[RGB_CHANNELS] = {0, 0, 0};
			float upperAlpha = 0;
			if (upper->props.visible) {
				sample_with_opacity(upper, x, y, upperOpacity, upperColor, &upperAlpha);
			}
			if (clipUpper) {
				upperAlpha *= lowerAlpha;
				for (int c = 0; c < RGB_CHANNELS; c++) {
					upperColor[c] *= lowerAlpha;
				}
			}
			u8 *pixel = canvas_pixel(lower->pixels, x, y);
			float alpha = upperAlpha + lowerAlpha * (1 - upperAlpha);
			if (alpha <= MIN_STORED_ALPHA) {
				memset(pixel, 0, RGBA_PIXEL_BYTES);
				continue;
			}
			float color[RGB_CHANNELS];
			for (int c = 0; c < RGB_CHANNELS; c++) {
				color[c] = upperColor[c] + lowerColor[c] * (1 - upperAlpha);
			}
			render_store_straight_rgba(color, alpha, pixel);
		}
	}
	lower->props.opacity = 100;
	lower->props.blend = BLEND_NORMAL;
	lower->props.visible = 1;
	lower->used = true;
	layer_scan_bounds(lower);
	layer_delete(layerIndex);
	g_document.current = layerIndex - 1;
	return MERGE_DONE;
}

// The group's layers composited at zoom 1, as a new pixel map. NULL when out of memory.
static u8 *render_group_pixels(int firstLayer, int lastLayer)
{
	u8 *pixels = undo_malloc(CANVAS_PIXELS * RGBA_PIXEL_BYTES);
	if (!pixels) {
		return NULL;
	}
	memset(pixels, 0, CANVAS_PIXELS * RGBA_PIXEL_BYTES);
	render_prep();
	for (int y = 0; y < CANVAS_HEIGHT; y++) {
		for (int x = 0; x < CANVAS_WIDTH; x++) {
			float color[RGB_CHANNELS];
			float alpha;
			render_group_sample(firstLayer, lastLayer, x, y, color, &alpha);
			if (alpha <= MIN_STORED_ALPHA) {
				continue;
			}
			render_store_straight_rgba(color, alpha, canvas_pixel(pixels, x, y));
		}
	}
	return pixels;
}

// Frees the layers firstLayer..lastLayer and puts the replacement in their place.
static void replace_layers(int firstLayer, int lastLayer, const Layer *replacement)
{
	layer_free(&g_document.layers[firstLayer]);
	g_document.layers[firstLayer] = *replacement;
	if (lastLayer > firstLayer) {
		layer_stack_remove_range(firstLayer + 1, lastLayer);
	}
}

bool group_merge(int groupId)
{
	int firstLayer = 0;
	int lastLayer = 0;
	if (!group_exists(groupId)) {
		return false;
	}
	if (!group_layer_range(groupId, &firstLayer, &lastLayer)) {
		return false;
	}
	u8 *pixels = render_group_pixels(firstLayer, lastLayer);
	if (!pixels) {
		return false;
	}
	Group *group = &g_document.groups[groupId];
	Layer merged;
	memset(&merged, 0, sizeof merged);
	merged.id = layer_new_id();
	snprintf(merged.props.name, sizeof merged.props.name, "%s", group->name);
	merged.props.type = LAYER_PIXEL;
	merged.props.visible = group->visible;
	merged.props.opacity = group->opacity;
	merged.props.blend = group->blend;
	merged.pixels = pixels;
	merged.used = true;
	layer_scan_bounds(&merged);
	replace_layers(firstLayer, lastLayer, &merged);
	group->exists = 0;
	g_document.current = firstLayer;
	return true;
}
