#include <stdlib.h>
#include <string.h>
#include "undo.h"
#include "document.h"
#include "math_util.h"

#define UNDO_STEPS_MAX 64
#define UNDO_MEMORY_CAP (20 * MEBIBYTE)
#define UNDO_FREE_ROOM (4 * MEBIBYTE) // kept free for saving and exporting, which zlib needs heap for
#define UNDO_STEP_FIRST_CAPACITY 16 // tile slots a new step starts with; it doubles when full
#define DISTANCE_PIXEL_BYTES 1 // one s8 distance value per pixel
#define UNDO_TILE_DISTANCE_BYTES (UNDO_TILE_SIZE * UNDO_TILE_SIZE * DISTANCE_PIXEL_BYTES)

typedef enum {
	TILE_COPY_FROM_LAYER,
	TILE_SWAP_WITH_LAYER,
} TileTransfer;

struct UndoStep {
	u32 layerId; // the step follows its layer when the layer moves
	int tileCount;
	int tileCapacity;
	u16 *tileIndices;
	u8 **tileCopies;
	size_t byteCount; // bytes of all tile copies
};

static UndoStep *g_undoSteps[UNDO_STEPS_MAX]; // oldest first
static UndoStep *g_redoSteps[UNDO_STEPS_MAX]; // the steps undo took back, newest last
static int g_undoCount;
static int g_redoCount;
static size_t g_undoBytes; // bytes the undo and redo steps hold

static size_t tile_copy_bytes(int layerType)
{
	size_t distanceBytes = layerType == LAYER_SDF ? UNDO_TILE_DISTANCE_BYTES : 0;
	return UNDO_TILE_PIXEL_BYTES + distanceBytes;
}

static IRect tile_rect(int tileIndex)
{
	int column = tileIndex % UNDO_TILE_COLUMNS;
	int row = tileIndex / UNDO_TILE_COLUMNS;
	int left = column * UNDO_TILE_SIZE;
	int top = row * UNDO_TILE_SIZE;
	return (IRect){.x0 = left, .y0 = top, .x1 = left + UNDO_TILE_SIZE, .y1 = top + UNDO_TILE_SIZE};
}

static void swap_bytes(u8 *first, u8 *second, int count)
{
	for (int i = 0; i < count; i++) {
		u8 saved = first[i];
		first[i] = second[i];
		second[i] = saved;
	}
}

// Moves one tile of a canvas-sized plane to or from its copy, which holds the tile's rows back to back.
static void transfer_tile_plane(u8 *plane, int pixelBytes, IRect tile, u8 *tilePlane, TileTransfer transfer)
{
	int rowBytes = UNDO_TILE_SIZE * pixelBytes;
	for (int row = 0; row < UNDO_TILE_SIZE; row++) {
		size_t canvasPixel = (size_t)(tile.y0 + row) * CANVAS_WIDTH + tile.x0;
		u8 *planeRow = plane + canvasPixel * pixelBytes;
		u8 *copyRow = tilePlane + row * rowBytes;
		if (transfer == TILE_COPY_FROM_LAYER) {
			memcpy(copyRow, planeRow, rowBytes);
		} else {
			swap_bytes(planeRow, copyRow, rowBytes);
		}
	}
}

static void transfer_tile(Layer *layer, IRect tile, u8 *tileCopy, TileTransfer transfer)
{
	transfer_tile_plane(layer->pixels, RGBA_PIXEL_BYTES, tile, tileCopy, transfer);
	if (layer->props.type != LAYER_SDF) {
		return;
	}
	u8 *distance = (u8 *)layer->distance;
	u8 *distanceCopy = tileCopy + UNDO_TILE_PIXEL_BYTES;
	transfer_tile_plane(distance, DISTANCE_PIXEL_BYTES, tile, distanceCopy, transfer);
}

UndoStep *undo_step_create(const Layer *layer)
{
	UndoStep *step = calloc(1, sizeof(UndoStep));
	if (!step) {
		return NULL;
	}
	step->layerId = layer->id;
	return step;
}

// Doubles the step's tile slots. False when out of memory.
static bool grow_tile_slots(UndoStep *step)
{
	int newCapacity = step->tileCapacity ? step->tileCapacity * 2 : UNDO_STEP_FIRST_CAPACITY;
	u16 *indices = realloc(step->tileIndices, newCapacity * sizeof *indices);
	if (!indices) {
		return false;
	}
	step->tileIndices = indices;
	u8 **copies = realloc(step->tileCopies, newCapacity * sizeof *copies);
	if (!copies) {
		return false;
	}
	step->tileCopies = copies;
	step->tileCapacity = newCapacity;
	return true;
}

const u8 *undo_step_add_tile(UndoStep *step, Layer *layer, int tileIndex)
{
	if (step->tileCount == step->tileCapacity && !grow_tile_slots(step)) {
		return NULL;
	}
	size_t copyBytes = tile_copy_bytes(layer->props.type);
	u8 *copy = undo_malloc(copyBytes);
	if (!copy) {
		return NULL;
	}
	transfer_tile(layer, tile_rect(tileIndex), copy, TILE_COPY_FROM_LAYER);
	int slot = step->tileCount;
	step->tileIndices[slot] = (u16)tileIndex;
	step->tileCopies[slot] = copy;
	step->tileCount++;
	step->byteCount += copyBytes;
	g_undoBytes += copyBytes;
	return copy;
}

int undo_step_tile_count(const UndoStep *step)
{
	return step->tileCount;
}

int undo_step_tile_index(const UndoStep *step, int slot)
{
	return step->tileIndices[slot];
}

void undo_step_free(UndoStep *step)
{
	for (int i = 0; i < step->tileCount; i++) {
		free(step->tileCopies[i]);
	}
	free(step->tileIndices);
	free(step->tileCopies);
	g_undoBytes -= step->byteCount;
	free(step);
}

// A full stack forgets its oldest step.
static void push_step(UndoStep **stack, int *count, UndoStep *step)
{
	if (*count == UNDO_STEPS_MAX) {
		undo_step_free(stack[0]);
		memmove(stack, stack + 1, (UNDO_STEPS_MAX - 1) * sizeof *stack);
		(*count)--;
	}
	stack[*count] = step;
	(*count)++;
}

static void clear_redo_steps(void)
{
	while (g_redoCount) {
		g_redoCount--;
		undo_step_free(g_redoSteps[g_redoCount]);
	}
}

void undo_clear(void)
{
	clear_redo_steps();
	while (g_undoCount) {
		g_undoCount--;
		undo_step_free(g_undoSteps[g_undoCount]);
	}
}

// Keeps the order of the other steps. Steps on different layers do not depend on each other.
static void forget_layer_steps(UndoStep **stack, int *count, u32 layerId)
{
	int kept = 0;
	for (int i = 0; i < *count; i++) {
		if (stack[i]->layerId == layerId) {
			undo_step_free(stack[i]);
		} else {
			stack[kept] = stack[i];
			kept++;
		}
	}
	*count = kept;
}

void undo_forget_layer(u32 layerId)
{
	forget_layer_steps(g_undoSteps, &g_undoCount, layerId);
	forget_layer_steps(g_redoSteps, &g_redoCount, layerId);
}

static bool tile_has_alpha(const Layer *layer, IRect tile)
{
	for (int y = tile.y0; y < tile.y1; y++) {
		const u8 *pixel = layer->pixels + ((size_t)y * CANVAS_WIDTH + tile.x0) * RGBA_PIXEL_BYTES;
		for (int x = tile.x0; x < tile.x1; x++) {
			if (pixel[RGBA_ALPHA_OFFSET]) {
				return true;
			}
			pixel += RGBA_PIXEL_BYTES;
		}
	}
	return false;
}

bool undo_push_colored_tiles(Layer *layer)
{
	UndoStep *step = undo_step_create(layer);
	if (!step) {
		return false;
	}
	for (int tileIndex = 0; tileIndex < UNDO_TILE_COLUMNS * UNDO_TILE_ROWS; tileIndex++) {
		if (tile_has_alpha(layer, tile_rect(tileIndex)) && !undo_step_add_tile(step, layer, tileIndex)) {
			undo_step_free(step);
			return false;
		}
	}
	if (!step->tileCount) {
		undo_step_free(step);
		return true;
	}
	undo_push(step);
	return true;
}

// Frees the redo steps, else the oldest undo step. False when there is nothing left to free.
static bool release_undo_memory(void)
{
	if (g_redoCount) {
		clear_redo_steps();
		return true;
	}
	if (!g_undoCount) {
		return false;
	}
	undo_step_free(g_undoSteps[0]);
	memmove(g_undoSteps, g_undoSteps + 1, (g_undoCount - 1) * sizeof *g_undoSteps);
	g_undoCount--;
	return true;
}

void *undo_malloc(size_t size)
{
	void *block = malloc(size);
	while (!block && release_undo_memory()) {
		block = malloc(size);
	}
	return block;
}

void undo_push(UndoStep *step)
{
	clear_redo_steps();
	push_step(g_undoSteps, &g_undoCount, step);
	while (g_undoBytes > UNDO_MEMORY_CAP && g_undoCount > 1) {
		release_undo_memory();
	}
	void *room;
	while (!(room = malloc(UNDO_FREE_ROOM)) && g_undoCount > 1) {
		release_undo_memory();
	}
	free(room);
}

// The step's layer in the stack, -1 when it is gone.
static int step_layer_index(const UndoStep *step)
{
	for (int i = 0; i < g_document.layerCount; i++) {
		if (g_document.layers[i].id == step->layerId) {
			return i;
		}
	}
	return -1;
}

// Exchanges the step's tile copies with the layer's tiles, so the same step serves undo and redo.
static void swap_step_tiles(UndoStep *step, int layerIndex, IRect *swappedRect)
{
	if (layerIndex < 0) {
		return;
	}
	Layer *layer = &g_document.layers[layerIndex];
	if (!layer->pixels || (layer->props.type == LAYER_SDF && !layer->distance)) {
		return;
	}
	for (int i = 0; i < step->tileCount; i++) {
		IRect tile = tile_rect(step->tileIndices[i]);
		transfer_tile(layer, tile, step->tileCopies[i], TILE_SWAP_WITH_LAYER);
		layer_grow_bounds(layer, tile);
		rect_union(swappedRect, tile);
	}
	layer->used = true;
}

bool undo_apply_undo(IRect *swappedRect, int *layerIndex)
{
	*swappedRect = (IRect){0, 0, 0, 0};
	if (!g_undoCount) {
		return false;
	}
	g_undoCount--;
	UndoStep *step = g_undoSteps[g_undoCount];
	*layerIndex = step_layer_index(step);
	swap_step_tiles(step, *layerIndex, swappedRect);
	push_step(g_redoSteps, &g_redoCount, step);
	return true;
}

bool undo_apply_redo(IRect *swappedRect, int *layerIndex)
{
	*swappedRect = (IRect){0, 0, 0, 0};
	if (!g_redoCount) {
		return false;
	}
	g_redoCount--;
	UndoStep *step = g_redoSteps[g_redoCount];
	*layerIndex = step_layer_index(step);
	swap_step_tiles(step, *layerIndex, swappedRect);
	push_step(g_undoSteps, &g_undoCount, step);
	return true;
}

int undo_count(void)
{
	return g_undoCount;
}

int undo_redo_count(void)
{
	return g_redoCount;
}
