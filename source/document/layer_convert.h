// Turning a layer into pixels or into a shape (SDF) layer, and merging a layer down or a group into one
// pixel layer.
#pragma once
#include <stdbool.h>

// What layer_merge_down did, or why it could not.
enum {
	MERGE_DONE,
	MERGE_NO_LAYER_BELOW,
	MERGE_NEEDS_NORMAL_BLEND, // both layers need Normal blend
	MERGE_BELOW_HIDDEN,
	MERGE_OTHER_CONTAINER, // across a group's edge, or a normal layer into a clipped one
	MERGE_NO_MEMORY
};

// Freezes an SDF layer's look at zoom 1 (colour map, outline, shadow) into pixels.
bool layer_to_pixel(int layerIndex);
// Pixels with at least half alpha become the shape; their colours stay as the colour map.
bool layer_to_sdf(int layerIndex);
// Merges the layer into the one below it, as pixels. Returns a MERGE_* value.
int layer_merge_down(int layerIndex);
// Flattens a group into one pixel layer that takes the group's name, visibility, opacity and blend.
bool group_merge(int groupId);
