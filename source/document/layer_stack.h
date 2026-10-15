// Editing the layer stack: select, add, delete, move, drop, duplicate, rename, recolour, clip, and groups.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "document.h"

// Makes a layer current. False when the index is out of range or already current.
bool layer_select(int index);
// Adds a layer above the current one and makes it current. fillColor is saved as the layer's fill colour.
// Returns its index, -1 when the stack is full.
int layer_add(int type, const u8 fillColor[3]);
bool layer_delete(int index);
// Frees the layers bottom..top and closes the gap. The caller fixes the current layer and the groups.
void layer_stack_remove_range(int bottom, int top);
// One step up (direction > 0) or down. A grouped layer moves inside its group and steps out of it at the
// group's edge; an ungrouped layer steps over a whole group.
bool layer_move(int index, int direction);
int layer_duplicate(int index);
bool layer_rename(int index, const char *name);
void layer_recolor(int index, const u8 color[3]);
// Clipping: a clipped layer only shows where its base, the nearest non-clipped layer below it in the same
// container (the root or one group), has alpha. A layer directly above a group clips to the whole group.
bool layer_set_clip(int index, bool clipped); // false when there is nothing below it to clip to
// Groups: flat. A new layer added above a grouped layer joins its group. Group ids in use run from 1 to
// MAX_GROUPS.
bool group_exists(int groupId);
// A loaded layer still uses this group: it becomes visible and named after its id again, and keeps its saved
// blend mode, expanded state and padding.
void group_revive(Group *group, int groupId);
int group_create(int layerIndex); // wraps an ungrouped layer into a new group: its id, -1 on failure
// groupId 0 takes the layer out; the stack is rearranged to keep groups contiguous.
bool layer_set_group(int index, int groupId);
bool group_delete(int groupId, bool keepLayers);
bool group_layer_range(int groupId, int *bottom, int *top); // stack index range of a group's layers
bool group_rename(int groupId, const char *name);
// Drag and drop. `gap` is the slot in the stack the layer is dropped into: 0 = below the bottom layer, n =
// above the top one, k = between layer k-1 and layer k (numbered as they are before the move). intoGroup: -1
// lets the gap decide (a gap strictly between two layers of one group joins that group, any other gap is the
// root), 0 forces the root, g > 0 forces group g (the layer snaps to the nearest slot inside or at the edge
// of the group). Groups always stay contiguous, a layer that changes container loses its clip flag, and the
// current layer follows the moved one.
bool layer_drop(int from, int gap, int intoGroup);
// Moves a whole group (a gap inside it snaps to its nearest edge, inside another group hops above it).
bool group_move(int groupId, int gap);
