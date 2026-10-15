#include <stdio.h>
#include <string.h>
#include "layer_stack.h"
#include "ascii.h"
#include "document.h"
#include "undo.h"

// The original name is cut to ten characters so that " copy" fits in LAYER_NAME_SIZE.
#define COPY_NAME_FORMAT "%.10s copy"
#define GROUP_NAME_FORMAT "Group %d"

bool group_exists(int groupId)
{
	return groupId >= 1 && groupId <= MAX_GROUPS && g_document.groups[groupId].exists;
}

// A visible, expanded group named after its id.
static void group_init(Group *group, int groupId)
{
	memset(group, 0, sizeof *group);
	group->exists = 1;
	group->visible = 1;
	group->opacity = 100;
	group->expanded = 1;
	snprintf(group->name, sizeof group->name, GROUP_NAME_FORMAT, groupId);
}

void group_revive(Group *group, int groupId)
{
	group->exists = 1;
	group->visible = 1;
	group->opacity = 100;
	snprintf(group->name, sizeof group->name, GROUP_NAME_FORMAT, groupId);
}

// Puts a layer into the stack at index, shifts the layers from there up, and makes it current.
static int insert_layer(int index, const Layer *layer)
{
	int layersAbove = g_document.layerCount - index;
	memmove(&g_document.layers[index + 1], &g_document.layers[index], layersAbove * sizeof(Layer));
	g_document.layers[index] = *layer;
	g_document.layerCount++;
	g_document.current = index;
	return index;
}

int layer_add(int type, const u8 fillColor[3])
{
	if (g_document.layerCount >= MAX_LAYERS) {
		return -1;
	}
	int insertAt = g_document.layerCount ? g_document.current + 1 : 0;
	Layer layer;
	memset(&layer, 0, sizeof layer);
	layer_defaults(&layer.props, type, fillColor);
	if (g_document.layerCount) {
		layer.props.group = g_document.layers[g_document.current].props.group;
	}
	if (!layer_alloc(&layer, type)) {
		layer_free(&layer);
		return -1;
	}
	return insert_layer(insertAt, &layer);
}

static void remove_empty_groups(void)
{
	for (int groupId = 1; groupId <= MAX_GROUPS; groupId++) {
		if (!g_document.groups[groupId].exists) {
			continue;
		}
		int bottom = 0;
		int top = 0;
		if (!group_layer_range(groupId, &bottom, &top)) {
			g_document.groups[groupId].exists = 0;
		}
	}
}

bool group_layer_range(int groupId, int *bottom, int *top)
{
	int bottomIndex = -1;
	int topIndex = -1;
	for (int i = 0; i < g_document.layerCount; i++) {
		if (g_document.layers[i].props.group == groupId) {
			if (bottomIndex < 0) {
				bottomIndex = i;
			}
			topIndex = i;
		}
	}
	if (bottomIndex < 0) {
		return false;
	}
	if (bottom) {
		*bottom = bottomIndex;
	}
	if (top) {
		*top = topIndex;
	}
	return true;
}

bool layer_delete(int index)
{
	if (g_document.layerCount <= 1 || index < 0 || index >= g_document.layerCount) {
		return false;
	}
	layer_free(&g_document.layers[index]);
	int layersAbove = g_document.layerCount - index - 1;
	memmove(&g_document.layers[index], &g_document.layers[index + 1], layersAbove * sizeof(Layer));
	g_document.layerCount--;
	memset(&g_document.layers[g_document.layerCount], 0, sizeof(Layer));
	if (g_document.current >= g_document.layerCount) {
		g_document.current = g_document.layerCount - 1;
	} else if (g_document.current > index) {
		g_document.current--;
	}
	remove_empty_groups();
	return true;
}

// Moves the layer at index `from` to index `to`; the layers between shift by one and the current layer
// follows. Returns the new index.
static int move_layer(int from, int to)
{
	if (from == to) {
		return from;
	}
	Layer moved = g_document.layers[from];
	if (from < to) {
		memmove(&g_document.layers[from], &g_document.layers[from + 1], (to - from) * sizeof(Layer));
	} else {
		memmove(&g_document.layers[to + 1], &g_document.layers[to], (from - to) * sizeof(Layer));
	}
	g_document.layers[to] = moved;
	int current = g_document.current;
	if (current == from) {
		current = to;
	} else if (from < to && current > from && current <= to) {
		current--;
	} else if (from > to && current >= to && current < from) {
		current++;
	}
	g_document.current = current;
	return to;
}

bool layer_move(int index, int direction)
{
	int target = index + direction;
	if (index < 0 || index >= g_document.layerCount || target < 0 || target >= g_document.layerCount) {
		return false;
	}
	int groupId = g_document.layers[index].props.group;
	if (groupId) {
		if (g_document.layers[target].props.group == groupId) {
			move_layer(index, target);
		} else {
			// At the group's edge the layer only leaves the group: it already sits outside it.
			g_document.layers[index].props.group = 0;
			remove_empty_groups();
		}
		return true;
	}
	int targetGroup = g_document.layers[target].props.group;
	if (!targetGroup) {
		move_layer(index, target);
		return true;
	}
	int bottom = 0;
	int top = 0;
	group_layer_range(targetGroup, &bottom, &top);
	move_layer(index, direction > 0 ? top : bottom);
	return true;
}

// Copies a name without its leading and trailing spaces. It stops at the first character that is not
// printable ASCII and when the destination is full.
static void copy_clean_name(char *destination, size_t capacity, const char *name)
{
	while (*name == ' ') {
		name++;
	}
	size_t length = 0;
	while (name[length] && length < capacity - 1 && ascii_is_printable(name[length])) {
		destination[length] = name[length];
		length++;
	}
	while (length > 0 && destination[length - 1] == ' ') {
		length--;
	}
	destination[length] = 0;
}

bool layer_select(int index)
{
	if (index < 0 || index >= g_document.layerCount || index == g_document.current) {
		return false;
	}
	g_document.current = index;
	return true;
}

bool layer_rename(int index, const char *name)
{
	if (index < 0 || index >= g_document.layerCount || !name) {
		return false;
	}
	char cleanName[LAYER_NAME_SIZE];
	copy_clean_name(cleanName, sizeof cleanName, name);
	if (!cleanName[0]) {
		return false; // keep the old name rather than an empty one
	}
	memcpy(g_document.layers[index].props.name, cleanName, sizeof cleanName);
	return true;
}

bool group_rename(int groupId, const char *name)
{
	if (!group_exists(groupId) || !name) {
		return false;
	}
	char cleanName[LAYER_NAME_SIZE];
	copy_clean_name(cleanName, sizeof cleanName, name);
	if (!cleanName[0]) {
		return false;
	}
	memcpy(g_document.groups[groupId].name, cleanName, sizeof cleanName);
	return true;
}

// Clamps a slot to the span of a group or its edges. `groups` holds the group of each layer, bottom first.
// The slot stays as it is when no layer is in the group.
static int snap_to_group_span(const int *groups, int count, int groupId, int slot)
{
	int bottom = -1;
	int top = -1;
	for (int i = 0; i < count; i++) {
		if (groups[i] == groupId) {
			if (bottom < 0) {
				bottom = i;
			}
			top = i;
		}
	}
	if (bottom < 0) {
		return slot;
	}
	if (slot < bottom) {
		return bottom;
	}
	if (slot > top + 1) {
		return top + 1;
	}
	return slot;
}

bool layer_drop(int from, int gap, int intoGroup)
{
	int layerCount = g_document.layerCount;
	if (from < 0 || from >= layerCount || gap < 0 || gap > layerCount ||
	    (intoGroup > 0 && !group_exists(intoGroup))) {
		return false;
	}
	int oldGroup = g_document.layers[from].props.group;
	// The groups of the other layers, bottom first.
	int otherGroups[MAX_LAYERS];
	int otherCount = 0;
	for (int i = 0; i < layerCount; i++) {
		if (i != from) {
			otherGroups[otherCount] = g_document.layers[i].props.group;
			otherCount++;
		}
	}
	// The slot among the other layers.
	int slot = gap > from ? gap - 1 : gap;
	// The group the slot lies strictly inside, 0 for none.
	int slotGroup = 0;
	if (slot > 0 && slot < otherCount && otherGroups[slot - 1] == otherGroups[slot]) {
		slotGroup = otherGroups[slot];
	}
	int targetGroup = intoGroup >= 0 ? intoGroup : slotGroup;
	if (targetGroup) {
		slot = snap_to_group_span(otherGroups, otherCount, targetGroup, slot);
	} else if (slotGroup) {
		// A drop forced to the root inside a group hops above the group.
		while (slot < otherCount && otherGroups[slot] == slotGroup) {
			slot++;
		}
	}
	if (slot == from && targetGroup == oldGroup) {
		return true;
	}
	int newIndex = move_layer(from, slot);
	if (targetGroup != oldGroup) {
		g_document.layers[newIndex].props.clipped = 0; // its clip base is in the old container
	}
	g_document.layers[newIndex].props.group = (u8)targetGroup;
	remove_empty_groups();
	return true;
}

// The group that has layers on both sides of a gap, 0 when the gap is not inside a group.
static int group_around_gap(int gap)
{
	if (gap <= 0 || gap >= g_document.layerCount) {
		return 0;
	}
	int groupBelow = g_document.layers[gap - 1].props.group;
	int groupAbove = g_document.layers[gap].props.group;
	if (groupBelow != groupAbove) {
		return 0;
	}
	return groupAbove;
}

// Moves the layers bottom..top as one block to a gap outside them; the current layer follows.
static void move_layer_block(int bottom, int top, int gap)
{
	static Layer block[MAX_LAYERS]; // kept off the stack
	int blockSize = top - bottom + 1;
	memcpy(block, &g_document.layers[bottom], blockSize * sizeof(Layer));
	int current = g_document.current;
	int newBottom;
	if (gap > top) {
		// Up: the layers in between shift down.
		int layersBetween = gap - top - 1;
		memmove(&g_document.layers[bottom], &g_document.layers[top + 1], layersBetween * sizeof(Layer));
		newBottom = gap - blockSize;
	} else {
		// Down: the layers in between shift up.
		int layersBetween = bottom - gap;
		memmove(&g_document.layers[gap + blockSize], &g_document.layers[gap], layersBetween * sizeof(Layer));
		newBottom = gap;
	}
	memcpy(&g_document.layers[newBottom], block, blockSize * sizeof(Layer));
	if (current >= bottom && current <= top) {
		g_document.current = newBottom + (current - bottom);
	} else if (gap > top && current > top && current < gap) {
		g_document.current = current - blockSize;
	} else if (gap < bottom && current >= gap && current < bottom) {
		g_document.current = current + blockSize;
	}
}

bool group_move(int groupId, int gap)
{
	int layerCount = g_document.layerCount;
	int bottom;
	int top;
	if (!group_exists(groupId) || !group_layer_range(groupId, &bottom, &top) || gap < 0 || gap > layerCount) {
		return false;
	}
	if (gap > bottom && gap <= top) {
		bool nearerBottom = gap - bottom < top + 1 - gap;
		gap = nearerBottom ? bottom : top + 1;
	}
	int surroundingGroup = group_around_gap(gap);
	if (surroundingGroup && surroundingGroup != groupId) {
		while (gap < layerCount && g_document.layers[gap].props.group == surroundingGroup) {
			gap++;
		}
	}
	if (gap == bottom || gap == top + 1) {
		return true;
	}
	move_layer_block(bottom, top, gap);
	return true;
}

// The lowest unused group id, 0 when all are taken.
static int free_group_id(void)
{
	for (int groupId = 1; groupId <= MAX_GROUPS; groupId++) {
		if (!g_document.groups[groupId].exists) {
			return groupId;
		}
	}
	return 0;
}

int group_create(int layerIndex)
{
	if (layerIndex < 0 || layerIndex >= g_document.layerCount || g_document.layers[layerIndex].props.group) {
		return -1;
	}
	int groupId = free_group_id();
	if (!groupId) {
		return -1;
	}
	group_init(&g_document.groups[groupId], groupId);
	g_document.layers[layerIndex].props.group = (u8)groupId;
	g_document.layers[layerIndex].props.clipped = 0;
	return groupId;
}

bool layer_set_group(int index, int groupId)
{
	if (index < 0 || index >= g_document.layerCount || groupId < 0 || groupId > MAX_GROUPS ||
	    (groupId && !g_document.groups[groupId].exists)) {
		return false;
	}
	int oldGroup = g_document.layers[index].props.group;
	if (groupId == oldGroup) {
		return true;
	}
	int newIndex = index;
	if (oldGroup) {
		// Leave the old group without splitting it: a layer in its middle first moves to its top.
		int bottom = 0;
		int top = 0;
		group_layer_range(oldGroup, &bottom, &top);
		if (index != bottom && index != top) {
			newIndex = move_layer(index, top);
		}
		g_document.layers[newIndex].props.group = 0;
	}
	if (groupId) {
		int bottom = 0;
		int top = 0;
		if (group_layer_range(groupId, &bottom, &top)) {
			if (newIndex < bottom) {
				newIndex = move_layer(newIndex, bottom - 1);
			} else if (newIndex > top) {
				newIndex = move_layer(newIndex, top + 1);
			}
		}
		g_document.layers[newIndex].props.group = (u8)groupId;
	}
	g_document.layers[newIndex].props.clipped = 0; // its clip base is in the old container
	remove_empty_groups();
	return true;
}

void layer_stack_remove_range(int bottom, int top)
{
	int removedCount = top - bottom + 1;
	for (int i = bottom; i <= top; i++) {
		layer_free(&g_document.layers[i]);
	}
	int layersAbove = g_document.layerCount - top - 1;
	memmove(&g_document.layers[bottom], &g_document.layers[top + 1], layersAbove * sizeof(Layer));
	memset(&g_document.layers[g_document.layerCount - removedCount], 0, removedCount * sizeof(Layer));
	g_document.layerCount -= removedCount;
}

bool group_delete(int groupId, bool keepLayers)
{
	int bottom = 0;
	int top = 0;
	if (!group_exists(groupId)) {
		return false;
	}
	if (!group_layer_range(groupId, &bottom, &top)) {
		g_document.groups[groupId].exists = 0;
		return true;
	}
	if (keepLayers) {
		for (int i = bottom; i <= top; i++) {
			g_document.layers[i].props.group = 0;
		}
		g_document.groups[groupId].exists = 0;
		return true;
	}
	int groupSize = top - bottom + 1;
	if (groupSize >= g_document.layerCount) {
		return false; // never delete every layer
	}
	layer_stack_remove_range(bottom, top);
	g_document.groups[groupId].exists = 0;
	if (g_document.current > top) {
		g_document.current -= groupSize;
	} else if (g_document.current >= bottom) {
		// The current layer is deleted: the layer above the group takes over, else the top layer.
		g_document.current = bottom < g_document.layerCount ? bottom : g_document.layerCount - 1;
	}
	return true;
}

// A grouped layer clips only to a layer of its own group; an ungrouped one to whatever is below it.
static bool has_clip_base(int index)
{
	if (index == 0) {
		return false;
	}
	int groupId = g_document.layers[index].props.group;
	if (!groupId) {
		return true;
	}
	return g_document.layers[index - 1].props.group == groupId;
}

bool layer_set_clip(int index, bool clipped)
{
	if (index < 0 || index >= g_document.layerCount) {
		return false;
	}
	if (clipped && !has_clip_base(index)) {
		return false;
	}
	g_document.layers[index].props.clipped = clipped;
	return true;
}

int layer_duplicate(int index)
{
	if (g_document.layerCount >= MAX_LAYERS || index < 0 || index >= g_document.layerCount) {
		return -1;
	}
	const Layer *source = &g_document.layers[index];
	Layer copy;
	memset(&copy, 0, sizeof copy);
	copy.props = source->props;
	if (!layer_alloc(&copy, copy.props.type)) {
		layer_free(&copy);
		return -1;
	}
	memcpy(copy.pixels, source->pixels, CANVAS_PIXELS * RGBA_PIXEL_BYTES);
	if (copy.distance) {
		memcpy(copy.distance, source->distance, CANVAS_PIXELS);
	}
	copy.used = source->used;
	copy.bounds = source->bounds;
	memcpy(copy.occupancy, source->occupancy, sizeof copy.occupancy);
	snprintf(copy.props.name, sizeof copy.props.name, COPY_NAME_FORMAT, source->props.name);
	return insert_layer(index + 1, &copy);
}

void layer_recolor(int index, const u8 color[3])
{
	if (index < 0 || index >= g_document.layerCount || !g_document.layers[index].pixels) {
		return;
	}
	Layer *layer = &g_document.layers[index];
	// Without memory for its own undo step, the layer's older steps would bring back the old colour in places.
	if (!undo_push_colored_tiles(layer)) {
		undo_forget_layer(layer->id);
	}
	u8 *pixel = layer->pixels;
	for (size_t i = 0; i < CANVAS_PIXELS; i++) {
		if (pixel[RGBA_ALPHA_OFFSET]) {
			pixel[0] = color[0];
			pixel[1] = color[1];
			pixel[2] = color[2];
		}
		pixel += RGBA_PIXEL_BYTES;
	}
}
