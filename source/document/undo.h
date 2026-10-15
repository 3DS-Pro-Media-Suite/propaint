// Tile-based undo. A step keeps copies of only the tiles a stroke touched, taken before it changes them.
// Undo swaps the copies back into the layer and keeps what it swapped out, which is the redo.
// A tile copy holds UNDO_TILE_PIXEL_BYTES of RGBA pixels, then on an SDF layer one distance byte per pixel.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include "document.h"

#define UNDO_TILE_SIZE 32 // tile edge in canvas pixels
#define UNDO_TILE_COLUMNS (CANVAS_WIDTH / UNDO_TILE_SIZE)
#define UNDO_TILE_ROWS (CANVAS_HEIGHT / UNDO_TILE_SIZE)
#define UNDO_TILE_PIXEL_BYTES (UNDO_TILE_SIZE * UNDO_TILE_SIZE * RGBA_PIXEL_BYTES)

// The tiles one stroke changed, copied before it changed them. A tile index is row * UNDO_TILE_COLUMNS +
// column.
typedef struct UndoStep UndoStep;

// An empty step for a stroke on that layer. NULL when out of memory.
UndoStep *undo_step_create(const Layer *layer);
// Copies one tile of the layer into the step. Returns the copy, NULL when out of memory. The copy stays where
// it is until the step is freed.
const u8 *undo_step_add_tile(UndoStep *step, Layer *layer, int tileIndex);
int undo_step_tile_count(const UndoStep *step);
int undo_step_tile_index(const UndoStep *step, int slot); // the tile index of the copy in that slot
void undo_step_free(UndoStep *step);
// Puts a finished stroke's step on the undo stack. Frees the redo steps, and old steps while memory is short.
void undo_push(UndoStep *step);
void undo_clear(void);
// Drops the undo and redo steps of one layer: it is gone, or its content changed outside the undo steps.
void undo_forget_layer(u32 layerId);
// Pushes a step with every tile of the layer that holds colour, before an edit of the whole layer.
// False when out of memory.
bool undo_push_colored_tiles(Layer *layer);
// malloc that frees the oldest undo steps until the block fits.
void *undo_malloc(size_t size);
// Both report the canvas rect of the tiles they swapped (empty if none) and the layer they belong to.
bool undo_apply_undo(IRect *swappedRect, int *layerIndex);
bool undo_apply_redo(IRect *swappedRect, int *layerIndex);
int undo_count(void);
int undo_redo_count(void);
