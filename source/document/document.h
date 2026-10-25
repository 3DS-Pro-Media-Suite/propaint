// The document in memory: a stack of layers on a CANVAS_WIDTH x CANVAS_HEIGHT canvas, the current layer, the
// memory and drawn bounds of each layer, and the project (.propaint) file.
// A pixel layer holds RGBA8 pixels.
// An SDF layer holds the same RGBA8 map (colour and density) plus a signed distance field: one s8 per pixel,
// the distance to the shape's edge in 1/SDF_UNITS_PER_PIXEL canvas px, negative inside. The field decides
// where the edge is and the RGBA map gives the colour inside it, so one SDF layer can hold many colours.
// The renderer samples and thresholds the field per screen pixel, so SDF edges stay crisp at any zoom.
#pragma once
#include <3ds.h>
#include <stdbool.h>

#define CANVAS_WIDTH 640
#define CANVAS_HEIGHT 480
#define CANVAS_PIXELS ((size_t)CANVAS_WIDTH * CANVAS_HEIGHT)
#define MAX_LAYERS 12
#define MAX_GROUPS 8 // group ids are 1..MAX_GROUPS (0 = not in a group)
#define SDF_UNITS_PER_PIXEL 8.0f // field units per canvas pixel
#define SDF_FAR 127 // "no edge nearby", and the largest distance an s8 field value holds
#define RGBA_PIXEL_BYTES 4 // layer pixels are RGBA8
#define RGBA_ALPHA_OFFSET 3 // the alpha byte of an RGBA8 pixel
#define LAYER_NAME_SIZE 16 // a layer's or a group's name with its terminating zero
#define INK_COLOR_DEFAULT {24, 26, 52} // the colour a fresh start paints with
// The ranges of a layer's and a group's look. A loaded file is clamped to them.
#define OPACITY_MAX 100 // percent
#define SOFTNESS_MAX 16 // half pixels
#define MELT_RADIUS_MAX 8
#define OUTLINE_WIDTH_MAX 12
#define SHADOW_OFFSET_MAX 12
#define SHADOW_BLUR_MAX 8

enum {
	LAYER_PIXEL,
	LAYER_SDF
};

enum {
	BLEND_NORMAL,
	BLEND_MULTIPLY,
	BLEND_ADD,
	BLEND_COUNT
};

typedef struct { // saved verbatim in the project file
	char name[LAYER_NAME_SIZE];
	u8 type;
	u8 visible;
	u8 locked;
	u8 blend;
	u8 opacity; // 0..100
	u8 fillColor[3]; // the colour at creation, saved but never read: SDF layers carry their own colour map
	u8 outlineWidth; // SDF: outline width in canvas px (0 = none)
	u8 outlineColor[3]; // SDF: outline colour
	u8 shadowOn; // SDF: drop shadow on
	u8 shadowBlur; // SDF: shadow blur in px
	u8 shadowAlpha; // SDF: shadow opacity 0..255
	s8 shadowX; // SDF: shadow offset in canvas px
	s8 shadowY;
	u8 shadowColor[3]; // SDF: shadow colour
	u8 softness; // SDF: edge softness in half pixels (0 = as crisp as the screen allows)
	u8 meltRadius; // SDF: how much overlapping strokes melt together, in px
	u8 clipped; // clipping mask: only shows where the layer below it (its base) has alpha
	u8 group; // 0 = none, else the group id; the layers of a group are contiguous in the stack
} LayerProps;

typedef struct { // x1 and y1 are exclusive
	int x0;
	int y0;
	int x1;
	int y1;
} IRect;

// The occupancy grid: cells of 16 x 16 canvas px that may hold something (conservative).
#define OCCUPANCY_CELL_SHIFT 4
#define OCCUPANCY_CELL_SIZE (1 << OCCUPANCY_CELL_SHIFT)
#define OCCUPANCY_WIDTH (CANVAS_WIDTH / OCCUPANCY_CELL_SIZE)
#define OCCUPANCY_HEIGHT (CANVAS_HEIGHT / OCCUPANCY_CELL_SIZE)

typedef struct {
	LayerProps props;
	u32 id; // unique while the app runs, so undo steps follow a layer when it moves; not saved
	u8 *pixels; // CANVAS_WIDTH * CANVAS_HEIGHT RGBA8 pixels, straight alpha (both layer types)
	s8 *distance; // LAYER_SDF only: CANVAS_WIDTH * CANVAS_HEIGHT field values
	bool used; // false until something is drawn; the renderer skips an unused layer
	// Bounds of what is drawn (empty: nothing). The renderer only looks inside them. Rebuilt on load.
	IRect bounds;
	// Cells that may hold content, padded for outline and shadow. The renderer skips the rest.
	u8 occupancy[OCCUPANCY_WIDTH * OCCUPANCY_HEIGHT];
} Layer;

typedef struct { // a group composites its layers as one unit, then applies its own opacity and blend mode
	char name[LAYER_NAME_SIZE];
	u8 exists; // groups[0] is never used
	u8 visible;
	u8 opacity; // 0..100
	u8 blend;
	u8 expanded; // expanded in the UI
	u8 padding[3]; // keeps the saved size
} Group;

typedef struct {
	Layer layers[MAX_LAYERS]; // layers[0] is the bottom layer
	Group groups[MAX_GROUPS + 1]; // indexed by group id
	int layerCount;
	int current; // index of the current layer
} Document;

extern Document g_document;

Layer *document_current_layer(void);
bool document_reset(void); // an empty document with the two starting layers; false when out of memory
// Saves through a temporary file, so a failed save keeps the old file.
bool document_save(const char *path);
// Replaces the document with a project file. On failure the document is unchanged.
bool document_load(const char *path);
// Allocates the cleared pixel map, and the distance field of an SDF layer. The layer gets a new id.
bool layer_alloc(Layer *layer, int type);
// Frees the layer's maps and forgets its undo steps.
void layer_free(Layer *layer);
u32 layer_new_id(void); // for a layer made without layer_alloc
// Default properties for a new layer with a numbered name. fillColor goes into props->fillColor.
void layer_defaults(LayerProps *props, int type, const u8 fillColor[3]);
// Rebuilds the bounds and the occupancy grid from the layer's data.
void layer_scan_bounds(Layer *layer);
// Grows the bounds and the occupancy grid to cover rect.
void layer_grow_bounds(Layer *layer, IRect rect);
// Grows target to cover rect. An empty target becomes rect; an empty rect changes nothing.
void rect_union(IRect *target, IRect rect);
