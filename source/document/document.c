#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "document.h"
#include "layer_stack.h"
#include "math_util.h"
#include "safe_file.h"
#include "undo.h"

// A drawn rect marks the occupancy grid this far around it, so an outline or a shadow is covered.
#define OCCUPANCY_MARK_MARGIN 24
// Scanned content marks this many cells around it, for the same reason.
#define OCCUPANCY_SCAN_PADDING_CELLS 2

#define PIXEL_MAP_BYTES (CANVAS_PIXELS * RGBA_PIXEL_BYTES)

#define LAYER_NUMBER_WRAP 100 // new layer names are numbered below this
#define DEFAULT_SHADOW_OFFSET 3
#define DEFAULT_SHADOW_BLUR 3
#define DEFAULT_SHADOW_ALPHA 128
#define DEFAULT_MELT_RADIUS 2

// Project file: PROJECT_MAGIC, width, height, layer count, current layer, the groups, then per layer its
// LayerProps and zlib blocks: the RGBA map, and for an SDF layer the distance field. A block starts with its
// u32 size.
#define PROJECT_MAGIC "PPD1"
#define PROJECT_MAGIC_SIZE 4
#define GROUP_SLOTS (MAX_GROUPS + 1)
#define BLOCK_COMPRESSION_LEVEL 1 // fastest
#define BLOCK_MAX_OVERHEAD 65536 // a block this much larger than its data is corrupt

Document g_document;

static int g_layerNameCounter; // numbers the names of new layers
static u32 g_lastLayerId; // 0 is never a layer's id

static void mark_occupied_cells(Layer *layer, IRect rect)
{
	int firstCellX = (rect.x0 - OCCUPANCY_MARK_MARGIN) >> OCCUPANCY_CELL_SHIFT;
	int lastCellX = (rect.x1 + OCCUPANCY_MARK_MARGIN) >> OCCUPANCY_CELL_SHIFT;
	int firstCellY = (rect.y0 - OCCUPANCY_MARK_MARGIN) >> OCCUPANCY_CELL_SHIFT;
	int lastCellY = (rect.y1 + OCCUPANCY_MARK_MARGIN) >> OCCUPANCY_CELL_SHIFT;
	if (firstCellX < 0) {
		firstCellX = 0;
	}
	if (firstCellY < 0) {
		firstCellY = 0;
	}
	if (lastCellX > OCCUPANCY_WIDTH - 1) {
		lastCellX = OCCUPANCY_WIDTH - 1;
	}
	if (lastCellY > OCCUPANCY_HEIGHT - 1) {
		lastCellY = OCCUPANCY_HEIGHT - 1;
	}
	int rowCells = lastCellX >= firstCellX ? lastCellX - firstCellX + 1 : 0;
	for (int cellY = firstCellY; cellY <= lastCellY; cellY++) {
		memset(layer->occupancy + cellY * OCCUPANCY_WIDTH + firstCellX, 1, rowCells);
	}
}

static bool rect_is_empty(IRect rect)
{
	return rect.x1 <= rect.x0 || rect.y1 <= rect.y0;
}

static void extend_rect(IRect *target, IRect rect)
{
	if (rect.x0 < target->x0) {
		target->x0 = rect.x0;
	}
	if (rect.y0 < target->y0) {
		target->y0 = rect.y0;
	}
	if (rect.x1 > target->x1) {
		target->x1 = rect.x1;
	}
	if (rect.y1 > target->y1) {
		target->y1 = rect.y1;
	}
}

void layer_grow_bounds(Layer *layer, IRect rect)
{
	if (rect_is_empty(rect)) {
		return;
	}
	mark_occupied_cells(layer, rect);
	if (rect_is_empty(layer->bounds)) {
		layer->bounds = rect;
		return;
	}
	extend_rect(&layer->bounds, rect);
}

void rect_union(IRect *target, IRect rect)
{
	if (rect.x1 <= rect.x0) {
		return;
	}
	if (target->x1 <= target->x0) {
		*target = rect;
		return;
	}
	extend_rect(target, rect);
}

static bool pixel_has_content(const Layer *layer, size_t index)
{
	if (layer->pixels[index * RGBA_PIXEL_BYTES + RGBA_ALPHA_OFFSET]) {
		return true;
	}
	return layer->distance && layer->distance[index] < SDF_FAR;
}

// Marks the cells that hold content and returns the bounds of the content.
static IRect scan_content(Layer *layer)
{
	IRect empty = {0, 0, 0, 0};
	if (!layer->pixels) {
		return empty;
	}
	int minX = CANVAS_WIDTH;
	int minY = CANVAS_HEIGHT;
	int maxX = -1;
	int maxY = -1;
	for (int y = 0; y < CANVAS_HEIGHT; y++) {
		for (int x = 0; x < CANVAS_WIDTH; x++) {
			if (!pixel_has_content(layer, (size_t)y * CANVAS_WIDTH + x)) {
				continue;
			}
			int cell = (y >> OCCUPANCY_CELL_SHIFT) * OCCUPANCY_WIDTH + (x >> OCCUPANCY_CELL_SHIFT);
			layer->occupancy[cell] = 1;
			if (x < minX) {
				minX = x;
			}
			if (x > maxX) {
				maxX = x;
			}
			if (y < minY) {
				minY = y;
			}
			if (y > maxY) {
				maxY = y;
			}
		}
	}
	if (maxX < minX) {
		return empty;
	}
	IRect content = {.x0 = minX, .y0 = minY, .x1 = maxX + 1, .y1 = maxY + 1};
	return content;
}

static void mark_cell_and_neighbours(u8 *occupancy, int cellX, int cellY)
{
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			int x = cellX + dx;
			int y = cellY + dy;
			if (x >= 0 && x < OCCUPANCY_WIDTH && y >= 0 && y < OCCUPANCY_HEIGHT) {
				occupancy[y * OCCUPANCY_WIDTH + x] = 1;
			}
		}
	}
}

// Each pass grows the occupied cells by one cell in every direction.
static void pad_occupancy(Layer *layer)
{
	static u8 previousPass[OCCUPANCY_WIDTH * OCCUPANCY_HEIGHT];
	for (int pass = 0; pass < OCCUPANCY_SCAN_PADDING_CELLS; pass++) {
		memcpy(previousPass, layer->occupancy, sizeof previousPass);
		for (int cellY = 0; cellY < OCCUPANCY_HEIGHT; cellY++) {
			for (int cellX = 0; cellX < OCCUPANCY_WIDTH; cellX++) {
				if (previousPass[cellY * OCCUPANCY_WIDTH + cellX]) {
					mark_cell_and_neighbours(layer->occupancy, cellX, cellY);
				}
			}
		}
	}
}

void layer_scan_bounds(Layer *layer)
{
	memset(layer->occupancy, 0, sizeof layer->occupancy);
	layer->bounds = scan_content(layer);
	pad_occupancy(layer);
}

void layer_free(Layer *layer)
{
	undo_forget_layer(layer->id);
	free(layer->pixels);
	free(layer->distance);
	memset(layer, 0, sizeof *layer);
}

u32 layer_new_id(void)
{
	g_lastLayerId++;
	return g_lastLayerId;
}

bool layer_alloc(Layer *layer, int type)
{
	layer->id = layer_new_id();
	layer->props.type = type;
	layer->pixels = undo_malloc(PIXEL_MAP_BYTES);
	if (layer->pixels) {
		memset(layer->pixels, 0, PIXEL_MAP_BYTES);
	}
	if (type == LAYER_SDF) {
		layer->distance = undo_malloc(CANVAS_PIXELS);
		if (layer->distance) {
			memset(layer->distance, SDF_FAR, CANVAS_PIXELS);
		}
	}
	return layer->pixels && (type == LAYER_PIXEL || layer->distance);
}

void layer_defaults(LayerProps *props, int type, const u8 fillColor[3])
{
	memset(props, 0, sizeof *props);
	props->type = type;
	props->visible = 1;
	props->opacity = 100;
	g_layerNameCounter++;
	const char *typeName = type == LAYER_PIXEL ? "Pixels" : "Shape";
	snprintf(props->name, sizeof props->name, "%s %d", typeName, g_layerNameCounter % LAYER_NUMBER_WRAP);
	memcpy(props->fillColor, fillColor, sizeof props->fillColor);
	props->shadowX = DEFAULT_SHADOW_OFFSET;
	props->shadowY = DEFAULT_SHADOW_OFFSET;
	props->shadowBlur = DEFAULT_SHADOW_BLUR;
	props->shadowAlpha = DEFAULT_SHADOW_ALPHA;
	props->meltRadius = DEFAULT_MELT_RADIUS;
}

// Undo steps refer to the layers, so they go first.
static void free_document_layers(void)
{
	undo_clear();
	for (int i = 0; i < g_document.layerCount; i++) {
		layer_free(&g_document.layers[i]);
	}
}

bool document_reset(void)
{
	free_document_layers();
	memset(&g_document, 0, sizeof g_document);
	g_layerNameCounter = 0;
	static const u8 inkColor[3] = INK_COLOR_DEFAULT;
	bool pixelLayerAdded = layer_add(LAYER_PIXEL, inkColor) >= 0;
	bool shapeLayerAdded = layer_add(LAYER_SDF, inkColor) >= 0;
	return pixelLayerAdded && shapeLayerAdded;
}

static bool write_block(FILE *file, const void *data, uLong dataSize)
{
	uLong compressedSize = compressBound(dataSize);
	u8 *compressed = undo_malloc(compressedSize);
	if (!compressed) {
		return false;
	}
	bool ok = compress2(compressed, &compressedSize, data, dataSize, BLOCK_COMPRESSION_LEVEL) == Z_OK;
	u32 storedSize = (u32)compressedSize;
	ok = ok && fwrite(&storedSize, sizeof storedSize, 1, file) == 1 &&
	     fwrite(compressed, 1, compressedSize, file) == compressedSize;
	free(compressed);
	return ok;
}

static bool read_block(FILE *file, void *data, uLong dataSize)
{
	u32 compressedSize;
	if (fread(&compressedSize, sizeof compressedSize, 1, file) != 1) {
		return false;
	}
	if (compressedSize > dataSize + BLOCK_MAX_OVERHEAD) {
		return false;
	}
	// Ask for at least one byte: an empty allocation may return NULL.
	u8 *compressed = undo_malloc(compressedSize ? compressedSize : 1);
	if (!compressed) {
		return false;
	}
	uLongf unpackedSize = dataSize;
	bool ok = fread(compressed, 1, compressedSize, file) == compressedSize &&
	          uncompress(data, &unpackedSize, compressed, compressedSize) == Z_OK && unpackedSize == dataSize;
	free(compressed);
	return ok;
}

static bool write_header(FILE *file)
{
	u16 width = CANVAS_WIDTH;
	u16 height = CANVAS_HEIGHT;
	u8 layerCount = g_document.layerCount;
	u8 currentLayer = g_document.current;
	return fwrite(PROJECT_MAGIC, 1, PROJECT_MAGIC_SIZE, file) == PROJECT_MAGIC_SIZE &&
	       fwrite(&width, sizeof width, 1, file) == 1 && fwrite(&height, sizeof height, 1, file) == 1 &&
	       fwrite(&layerCount, sizeof layerCount, 1, file) == 1 &&
	       fwrite(&currentLayer, sizeof currentLayer, 1, file) == 1 &&
	       fwrite(g_document.groups, sizeof(Group), GROUP_SLOTS, file) == GROUP_SLOTS;
}

static bool write_layer(FILE *file, const Layer *layer)
{
	if (fwrite(&layer->props, sizeof layer->props, 1, file) != 1) {
		return false;
	}
	if (!write_block(file, layer->pixels, PIXEL_MAP_BYTES)) {
		return false;
	}
	if (layer->props.type != LAYER_SDF) {
		return true;
	}
	return write_block(file, layer->distance, CANVAS_PIXELS);
}

bool document_save(const char *path)
{
	char tempPath[SAFE_FILE_PATH_SIZE];
	safe_file_temp_path(path, tempPath);
	FILE *file = fopen(tempPath, "wb");
	if (!file) {
		return false;
	}
	bool ok = write_header(file);
	for (int i = 0; i < g_document.layerCount && ok; i++) {
		ok = write_layer(file, &g_document.layers[i]);
	}
	bool closed = fclose(file) == 0;
	return safe_file_commit(tempPath, path, closed && ok);
}

static bool read_header(FILE *file, Document *loaded, u8 *layerCount, u8 *currentLayer)
{
	char magic[PROJECT_MAGIC_SIZE];
	u16 width;
	u16 height;
	if (fread(magic, 1, PROJECT_MAGIC_SIZE, file) != PROJECT_MAGIC_SIZE) {
		return false;
	}
	if (memcmp(magic, PROJECT_MAGIC, PROJECT_MAGIC_SIZE) != 0) {
		return false;
	}
	if (fread(&width, sizeof width, 1, file) != 1 || fread(&height, sizeof height, 1, file) != 1) {
		return false;
	}
	if (fread(layerCount, sizeof *layerCount, 1, file) != 1 ||
	    fread(currentLayer, sizeof *currentLayer, 1, file) != 1) {
		return false;
	}
	if (width != CANVAS_WIDTH || height != CANVAS_HEIGHT) {
		return false;
	}
	if (*layerCount < 1 || *layerCount > MAX_LAYERS) {
		return false;
	}
	return fread(loaded->groups, sizeof(Group), GROUP_SLOTS, file) == GROUP_SLOTS;
}

// A file from elsewhere can hold any bytes. An unknown blend mode becomes Normal, as the renderer draws it.
static void repair_name_and_blend(char name[LAYER_NAME_SIZE], u8 *blend)
{
	name[LAYER_NAME_SIZE - 1] = '\0';
	if (*blend >= BLEND_COUNT) {
		*blend = BLEND_NORMAL;
	}
}

static void repair_group_names_and_blends(Document *document)
{
	for (int groupId = 0; groupId < GROUP_SLOTS; groupId++) {
		Group *group = &document->groups[groupId];
		repair_name_and_blend(group->name, &group->blend);
		group->opacity = min_int(group->opacity, OPACITY_MAX);
	}
}

// Look values outside the look sheet's ranges are clamped to them.
static void repair_layer_look(LayerProps *props)
{
	props->opacity = min_int(props->opacity, OPACITY_MAX);
	props->softness = min_int(props->softness, SOFTNESS_MAX);
	props->meltRadius = min_int(props->meltRadius, MELT_RADIUS_MAX);
	props->outlineWidth = min_int(props->outlineWidth, OUTLINE_WIDTH_MAX);
	props->shadowBlur = min_int(props->shadowBlur, SHADOW_BLUR_MAX);
	props->shadowX = clamp_int(props->shadowX, -SHADOW_OFFSET_MAX, SHADOW_OFFSET_MAX);
	props->shadowY = clamp_int(props->shadowY, -SHADOW_OFFSET_MAX, SHADOW_OFFSET_MAX);
}

static bool read_layer(FILE *file, Layer *layer)
{
	if (fread(&layer->props, sizeof layer->props, 1, file) != 1) {
		return false;
	}
	if (layer->props.type > LAYER_SDF) {
		return false;
	}
	repair_name_and_blend(layer->props.name, &layer->props.blend);
	repair_layer_look(&layer->props);
	if (!layer_alloc(layer, layer->props.type)) {
		return false;
	}
	if (!read_block(file, layer->pixels, PIXEL_MAP_BYTES)) {
		return false;
	}
	if (layer->props.type == LAYER_PIXEL) {
		return true;
	}
	return read_block(file, layer->distance, CANVAS_PIXELS);
}

static bool group_has_layer_below(const Document *document, int layerIndex, int groupId)
{
	for (int i = 0; i < layerIndex; i++) {
		if (document->layers[i].props.group == groupId) {
			return true;
		}
	}
	return false;
}

// Every layer's group must exist and the layers of a group must be contiguous.
static void repair_groups(Document *document, int layerCount)
{
	for (int i = 0; i < layerCount; i++) {
		LayerProps *props = &document->layers[i].props;
		int groupId = props->group;
		if (groupId > MAX_GROUPS) {
			props->group = 0;
			continue;
		}
		if (groupId == 0) {
			continue;
		}
		if (!document->groups[groupId].exists) {
			group_revive(&document->groups[groupId], groupId);
		}
		bool joinsLayerBelow = i > 0 && document->layers[i - 1].props.group == groupId;
		if (!joinsLayerBelow && group_has_layer_below(document, i, groupId)) {
			// A split group: this layer leaves it.
			props->group = 0;
		}
	}
}

bool document_load(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return false;
	}
	// Static because a Document is too big for the stack.
	static Document loaded;
	memset(&loaded, 0, sizeof loaded);
	u8 layerCount;
	u8 currentLayer;
	if (!read_header(file, &loaded, &layerCount, &currentLayer)) {
		fclose(file);
		return false;
	}
	bool ok = true;
	for (int i = 0; i < layerCount && ok; i++) {
		Layer *layer = &loaded.layers[i];
		ok = read_layer(file, layer);
		layer->used = true;
		layer_scan_bounds(layer);
		loaded.layerCount = i + 1;
	}
	fclose(file);
	if (!ok) {
		for (int i = 0; i < MAX_LAYERS; i++) {
			layer_free(&loaded.layers[i]);
		}
		return false;
	}
	repair_group_names_and_blends(&loaded);
	repair_groups(&loaded, layerCount);
	free_document_layers();
	g_document = loaded;
	g_document.current = currentLayer < layerCount ? currentLayer : layerCount - 1;
	g_layerNameCounter = layerCount;
	return true;
}

Layer *document_current_layer(void)
{
	return &g_document.layers[g_document.current];
}
