#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "brush_engine.h"
#include "color_units.h"
#include "document.h"
#include "math_util.h"
#include "render.h"
#include "undo.h"

#define RGBA_CHANNELS 4 // a direct dab's pixels are premultiplied floats
#define UNDO_TILE_COUNT (UNDO_TILE_COLUMNS * UNDO_TILE_ROWS)
#define PRESSURE_UNKNOWN -1

// Random numbers for jitter and scatter: xorshift32 with its usual seed and shifts.
#define RANDOM_SEED 2463534242u
#define RANDOM_SEED_LAYER_STEP 7919 // a prime, so each layer gets its own sequence
#define XORSHIFT_A 13
#define XORSHIFT_B 17
#define XORSHIFT_C 5
#define RANDOM_BITS 24 // a float holds this many bits exactly
#define RANDOM_MASK ((1u << RANDOM_BITS) - 1)
#define RANDOM_SCALE (1.0f / (1u << RANDOM_BITS))

// An integer hash of the pixel position gives the noise texture.
#define NOISE_HASH_X 374761393u
#define NOISE_HASH_Y 668265263u
#define NOISE_HASH_MIX 1274126177u
#define NOISE_HASH_SHIFT 13
#define NOISE_HASH_FINAL_SHIFT 16
#define NOISE_FINE_WEIGHT 0.6f // the noise mixes values per pixel
#define NOISE_COARSE_WEIGHT 0.4f // with values per 2 x 2 pixels; 1.0f - 0.6f would round one step lower

// A texture tile is read in 16.16 fixed point.
#define TEXTURE_FIXED_SHIFT 16
#define TEXTURE_FIXED_ONE (1u << TEXTURE_FIXED_SHIFT)
#define TEXTURE_SCALE_TO_UNIT 100.0f // textureScale is in percent

// Paint and recolour dabs.
#define TIP_FIELD_REACH 1.45f // in radii: covers a square tip's corners, which lie sqrt(2) radii out
#define TIP_FIELD_MARGIN 12.0f // canvas px past the tip where its field is written: the widest outline
#define TIP_CENTRE_GRADIENT 1e-6f // below this the pixel is the tip's centre
#define TIP_EDGE_MARGIN 2.0f // canvas px past the tip for its anti-aliased edge
#define TIP_COVERED_SQUARED_MARGIN 1.01f // a pixel this far past the covered reach, squared, is left out
#define CAPSULE_FIELD_MARGIN 12.0f // canvas px past a capsule where its distance field is written
#define SHORT_FIELD_MARGIN 4.0f // canvas px: the anti-aliased edge and the default melt (shortField)
#define CAPSULE_LENGTH_SQUARED_MIN 1e-6f // a shorter capsule is a circle
#define TIP_RATIO_MIN 0.1f // BRUSH_RATIO_MIN as a fraction; 10 * 0.01f would round one step lower

// The dab path.
#define DAB_RADIUS_MIN 0.6f // canvas px
#define DAB_STEP_MIN 1.0f // canvas px between two dabs
#define JITTER_SHRINK_MAX 0.8f // full jitter shrinks a dab by up to this part of its radius
#define PEN_MOVE_MIN 0.05f // canvas px; a shorter move keeps the speed and the direction
#define SPEED_KEEP 0.6f // the speed is smoothed: this part of the old value
#define SPEED_TAKE 0.4f // plus the rest from the new one; 1.0f - 0.6f would round one step lower
#define SPEED_FULL 0.8f // screen px per ms at which the speed sensor reaches its end
#define DIRECTION_LENGTH_MIN 0.01f // a shorter mean direction (a sharp turn) keeps the old one
#define CURVE_SEGMENTS (BRUSH_CURVE_POINTS - 1)

// Smudge and smooth dabs change the layer directly.
#define DIRECT_DAB_RADIUS_MAX 90 // canvas px
// The largest dab's box: per side, one pixel of margin and one of rounding.
#define DIRECT_DAB_BOX_MAX (2 * (DIRECT_DAB_RADIUS_MAX + 2))
#define DIRECT_DAB_BOX_PIXELS ((size_t)DIRECT_DAB_BOX_MAX * DIRECT_DAB_BOX_MAX)
#define DIRECT_DAB_EDGE 0.35f // the soft edge is this part of the radius, plus one pixel
#define DIRECT_DAB_WEIGHT_MIN 0.001f // a pixel with less weight is left alone
#define DIRECT_DAB_SPACING 0.25f // in radii
#define DIRECT_DAB_SKIPPED -1000 // a field value no pixel has: marks a pixel the dab leaves alone
#define SMOOTH_RADIUS 1 // smoothing averages the 3 x 3 neighbourhood
#define SMOOTH_WEIGHT (1.0f / ((2 * SMOOTH_RADIUS + 1) * (2 * SMOOTH_RADIUS + 1)))

static u8 *g_strokeCoverage; // the stroke's coverage of the colour map so far, opacity included, 0..255
static s8 *g_strokeShape; // SDF layers: the union of the stroke's dab shapes so far, SDF_FAR where none
static float *g_dabPixels; // a direct dab's new premultiplied pixels, until they are written back
static float *g_dabDistances; // a direct dab's new field values, or DIRECT_DAB_SKIPPED

// The stroke in progress.
static struct {
	bool active;
	StrokeSettings settings;
	int layerIndex;
	UndoStep *undoStep;
	const u8 *tileCopies
	        [UNDO_TILE_COUNT]; // each tile's pre-stroke copy in the undo step, NULL before it is saved
	bool hasLastDab;
	float lastDabX; // smudge drags along the move from the last dab
	float lastDabY;
	u32 randomState;
	float paintRate; // 0..1: smudge dabs mix this much of the stroke's colour into what they pick up
	u32 textureStep; // texture tile px per canvas px, 16.16 fixed point
	u32 lastTimeMs;
	bool hasSample; // the stroke's first pen sample has arrived
	float lastX; // the previous pen sample
	float lastY;
	float lastPressure;
	float nextDabDistance; // from the previous pen sample to the next dab, along the path
	float travelled; // the path length so far, for the fade sensor
	float speed; // smoothed, in screen px per ms
	float directionX; // smoothed unit direction of the stroke
	float directionY;
	float lastCapsuleRadius; // the SDF capsule's radius at the previous pen sample, 0 before the first
} g_stroke;

bool brush_engine_init(void)
{
	g_strokeCoverage = calloc(CANVAS_PIXELS, 1);
	g_strokeShape = malloc(CANVAS_PIXELS);
	g_dabPixels = malloc(DIRECT_DAB_BOX_PIXELS * RGBA_CHANNELS * sizeof(float));
	g_dabDistances = malloc(DIRECT_DAB_BOX_PIXELS * sizeof(float));
	if (!g_strokeCoverage || !g_strokeShape || !g_dabPixels || !g_dabDistances) {
		return false;
	}
	memset(g_strokeShape, SDF_FAR, CANVAS_PIXELS);
	return true;
}

bool brush_stroke_active(void)
{
	return g_stroke.active;
}

bool brush_stroke_begin(const StrokeSettings *settings)
{
	if (g_stroke.active || g_document.current >= g_document.layerCount) {
		return false;
	}
	Layer *layer = &g_document.layers[g_document.current];
	if (!layer->pixels) {
		return false;
	}
	g_stroke.undoStep = undo_step_create(layer);
	if (!g_stroke.undoStep) {
		return false;
	}
	for (int i = 0; i < UNDO_TILE_COUNT; i++) {
		g_stroke.tileCopies[i] = NULL;
	}
	g_stroke.active = true;
	g_stroke.settings = *settings;
	g_stroke.layerIndex = g_document.current;
	g_stroke.hasLastDab = false;
	g_stroke.hasSample = false;
	g_stroke.randomState = RANDOM_SEED ^ (u32)(g_document.current * RANDOM_SEED_LAYER_STEP);
	bool smudges = settings->mode == BRUSH_MODE_SMUDGE;
	g_stroke.paintRate = smudges ? settings->brush.paint * PERCENT_TO_UNIT : 0;
	int textureScale =
	        settings->brush.textureScale ? settings->brush.textureScale : BRUSH_TEXTURE_SCALE_DEFAULT;
	g_stroke.textureStep = (u32)(TEXTURE_FIXED_ONE * TEXTURE_SCALE_TO_UNIT / textureScale);
	layer->used = true;
	return true;
}

// Saves a tile's pre-stroke state in the undo step the first time the stroke touches it.
static bool backup_tile(Layer *layer, int tile)
{
	if (g_stroke.tileCopies[tile]) {
		return true;
	}
	g_stroke.tileCopies[tile] = undo_step_add_tile(g_stroke.undoStep, layer, tile);
	return g_stroke.tileCopies[tile] != NULL;
}

// How far past a shape the field is written. The full margin lets the layer's outline widen later.
static float field_margin(float fullMargin)
{
	return g_stroke.settings.shortField ? SHORT_FIELD_MARGIN : fullMargin;
}

static bool backup_rect(Layer *layer, IRect box)
{
	for (int tileY = box.y0 / UNDO_TILE_SIZE; tileY <= (box.y1 - 1) / UNDO_TILE_SIZE; tileY++) {
		for (int tileX = box.x0 / UNDO_TILE_SIZE; tileX <= (box.x1 - 1) / UNDO_TILE_SIZE; tileX++) {
			if (!backup_tile(layer, tileY * UNDO_TILE_COLUMNS + tileX)) {
				return false;
			}
		}
	}
	return true;
}

// The canvas pixels that a box in canvas coordinates touches, cut to the canvas.
static IRect canvas_box(float left, float top, float right, float bottom)
{
	IRect box = {
	        .x0 = max_int((int)floorf(left), 0),
	        .y0 = max_int((int)floorf(top), 0),
	        .x1 = min_int((int)ceilf(right), CANVAS_WIDTH),
	        .y1 = min_int((int)ceilf(bottom), CANVAS_HEIGHT),
	};
	return box;
}

// Quadratic smooth minimum: within k of each other, a and b blend instead of meeting in a crease.
static inline float smooth_min(float a, float b, float k)
{
	float smaller = a < b ? a : b;
	if (k <= 0) {
		return smaller;
	}
	float overlap = k - fabsf(a - b);
	if (overlap <= 0) {
		return smaller;
	}
	overlap /= k;
	return smaller - overlap * overlap * k / 4;
}

// A distance in field units as a field value. The caller has dropped distances of SDF_FAR and more.
static inline s8 to_field_value(float distance)
{
	return (s8)(distance < -SDF_FAR ? -SDF_FAR : distance);
}

static inline void clear_pixel(u8 *pixel)
{
	pixel[0] = 0;
	pixel[1] = 0;
	pixel[2] = 0;
	pixel[RGBA_ALPHA_OFFSET] = 0;
}

// The pre-stroke copy of the tile that holds canvas pixel (x, y). The stroke saves a tile before it first
// changes it.
static inline const u8 *saved_tile(int x, int y)
{
	int tile = (y / UNDO_TILE_SIZE) * UNDO_TILE_COLUMNS + x / UNDO_TILE_SIZE;
	return g_stroke.tileCopies[tile];
}

static inline const u8 *saved_pixel(int x, int y)
{
	const u8 *tileCopy = saved_tile(x, y);
	int tileMask = UNDO_TILE_SIZE - 1;
	return tileCopy + (size_t)((y & tileMask) * UNDO_TILE_SIZE + (x & tileMask)) * RGBA_PIXEL_BYTES;
}

// A tile copy holds the distance field after the pixels.
static inline s8 saved_distance(int x, int y)
{
	const u8 *tileCopy = saved_tile(x, y);
	int tileMask = UNDO_TILE_SIZE - 1;
	return (s8)tileCopy[UNDO_TILE_PIXEL_BYTES + (y & tileMask) * UNDO_TILE_SIZE + (x & tileMask)];
}

// Writes colour map pixel (x, y) from its pre-stroke value and the stroke's coverage so far.
static void write_color_pixel(Layer *layer, int x, int y, size_t index)
{
	const u8 *color = g_stroke.settings.color;
	const u8 *before = saved_pixel(x, y);
	u8 *out = layer->pixels + index * RGBA_PIXEL_BYTES;
	float coverage = g_strokeCoverage[index] * BYTE_TO_UNIT;
	float beforeAlpha = before[RGBA_ALPHA_OFFSET] * BYTE_TO_UNIT;
	if (g_stroke.settings.mode == BRUSH_MODE_RECOLOR) {
		out[RGBA_ALPHA_OFFSET] = before[RGBA_ALPHA_OFFSET];
		for (int c = 0; c < RGB_CHANNELS; c++) {
			// A transparent pixel keeps its colour.
			out[c] = before[RGBA_ALPHA_OFFSET] ? (u8)(before[c] + (color[c] - before[c]) * coverage + 0.5f)
			                                   : before[c];
		}
	} else if (g_stroke.settings.erase) {
		out[0] = before[0];
		out[1] = before[1];
		out[2] = before[2];
		out[RGBA_ALPHA_OFFSET] = (u8)(beforeAlpha * (1 - coverage) * UNIT_TO_BYTE + 0.5f);
	} else {
		float alpha = coverage + beforeAlpha * (1 - coverage);
		float inverseAlpha = 1 / alpha; // one division instead of three
		for (int c = 0; c < RGB_CHANNELS; c++) {
			float value =
			        (color[c] * coverage + before[c] * beforeAlpha * (1 - coverage)) * inverseAlpha + 0.5f;
			out[c] = value > 255 ? 255 : (u8)value;
		}
		out[RGBA_ALPHA_OFFSET] = (u8)(alpha * UNIT_TO_BYTE + 0.5f);
	}
}

// Writes the distance field at (x, y): the pre-stroke field united with, or minus, the stroke's shape so far.
static void write_distance(Layer *layer, int x, int y, size_t index, float meltDistance)
{
	float before = saved_distance(x, y);
	float shape = g_strokeShape[index];
	float result;
	if (g_stroke.settings.erase) {
		result = -smooth_min(-before, shape, meltDistance); // a smooth max(before, -shape)
	} else {
		result = smooth_min(before, shape, meltDistance);
	}
	layer->distance[index] = (s8)clamp_float(result, -SDF_FAR, SDF_FAR);
}

// Smudge picks its colour up at (sampleX, sampleY), behind the pen by the pen's last move.
static void smudge_source(const Layer *layer, float sampleX, float sampleY, bool isSdf,
                          float source[RGBA_CHANNELS], float *sourceDistance)
{
	render_pixels_bilinear(layer->pixels, sampleX, sampleY, source);
	if (isSdf) {
		float field = render_field_bilinear(layer->distance, sampleX, sampleY) * SDF_UNITS_PER_PIXEL;
		*sourceDistance = clamp_float(field, -SDF_FAR, SDF_FAR);
	}
}

// Smooth takes the average of the pixel's neighbourhood. The canvas edge repeats its pixels.
static void smooth_source(const Layer *layer, int x, int y, bool isSdf, float source[RGBA_CHANNELS],
                          float *sourceDistance)
{
	source[0] = 0;
	source[1] = 0;
	source[2] = 0;
	source[RGBA_ALPHA_OFFSET] = 0;
	float distanceSum = 0;
	for (int offsetY = -SMOOTH_RADIUS; offsetY <= SMOOTH_RADIUS; offsetY++) {
		for (int offsetX = -SMOOTH_RADIUS; offsetX <= SMOOTH_RADIUS; offsetX++) {
			int sampleX = clamp_int(x + offsetX, 0, CANVAS_WIDTH - 1);
			int sampleY = clamp_int(y + offsetY, 0, CANVAS_HEIGHT - 1);
			size_t sampleIndex = (size_t)sampleY * CANVAS_WIDTH + sampleX;
			const u8 *sample = layer->pixels + sampleIndex * RGBA_PIXEL_BYTES;
			float alpha = sample[RGBA_ALPHA_OFFSET] * BYTE_TO_UNIT;
			source[0] += sample[0] * alpha;
			source[1] += sample[1] * alpha;
			source[2] += sample[2] * alpha;
			source[RGBA_ALPHA_OFFSET] += sample[RGBA_ALPHA_OFFSET];
			if (isSdf) {
				distanceSum += layer->distance[sampleIndex];
			}
		}
	}
	for (int c = 0; c < RGBA_CHANNELS; c++) {
		source[c] *= SMOOTH_WEIGHT;
	}
	if (isSdf) {
		*sourceDistance = distanceSum * SMOOTH_WEIGHT;
	}
}

// Wet paint: what smudge picked up takes on some of the stroke's colour, and on an SDF layer some of its shape.
static void mix_in_paint(float source[RGBA_CHANNELS], float *sourceDistance, bool isSdf)
{
	const u8 *color = g_stroke.settings.color;
	float rate = g_stroke.paintRate;
	for (int c = 0; c < RGB_CHANNELS; c++) {
		source[c] += (color[c] - source[c]) * rate; // the paint is opaque, so premultiplied it is the colour
	}
	source[RGBA_ALPHA_OFFSET] += (UNIT_TO_BYTE - source[RGBA_ALPHA_OFFSET]) * rate;
	if (isSdf) {
		*sourceDistance += (-SDF_FAR - *sourceDistance) * rate;
	}
}

// The first pass of a direct dab at one pixel: moves its colour and field value toward the source by weight
// and keeps the result in the dab buffers. (smudgeX, smudgeY) is where smudge picks its colour up.
static void mix_direct_pixel(const Layer *layer, int x, int y, int boxIndex, float smudgeX, float smudgeY,
                             float weight, bool isSdf, bool isSmudge)
{
	float *mixed = g_dabPixels + boxIndex * RGBA_CHANNELS;
	size_t index = (size_t)y * CANVAS_WIDTH + x;
	const u8 *current = layer->pixels + index * RGBA_PIXEL_BYTES;
	float currentAlpha = current[RGBA_ALPHA_OFFSET] * BYTE_TO_UNIT;
	float source[RGBA_CHANNELS];
	float premultiplied[RGBA_CHANNELS] = {current[0] * currentAlpha, current[1] * currentAlpha,
	                                      current[2] * currentAlpha, (float)current[RGBA_ALPHA_OFFSET]};
	float currentDistance = isSdf ? layer->distance[index] : 0;
	float sourceDistance = currentDistance;
	if (isSmudge) {
		smudge_source(layer, smudgeX, smudgeY, isSdf, source, &sourceDistance);
		if (g_stroke.paintRate > 0) {
			mix_in_paint(source, &sourceDistance, isSdf);
		}
	} else {
		smooth_source(layer, x, y, isSdf, source, &sourceDistance);
	}
	for (int c = 0; c < RGBA_CHANNELS; c++) {
		mixed[c] = premultiplied[c] + (source[c] - premultiplied[c]) * weight;
	}
	g_dabDistances[boxIndex] = isSdf ? currentDistance + (sourceDistance - currentDistance) * weight : 0;
}

// Writes a premultiplied pixel with alpha 0..255 as a straight RGBA8 pixel.
static inline void write_straight_pixel(u8 *out, const float *pixel)
{
	float alpha = pixel[RGBA_ALPHA_OFFSET];
	if (alpha < 0.5f) {
		clear_pixel(out);
		return;
	}
	float unpremultiply = UNIT_TO_BYTE / alpha;
	for (int c = 0; c < RGB_CHANNELS; c++) {
		float value = pixel[c] * unpremultiply + 0.5f;
		out[c] = value > 255 ? 255 : (u8)value;
	}
	out[RGBA_ALPHA_OFFSET] = (u8)(alpha > 255 ? 255 : alpha + 0.5f);
}

// The second pass of a direct dab: writes the mixed pixels back. It waits until every pixel of the dab has
// read its neighbours.
static void store_direct_dab(Layer *layer, IRect box, bool isSdf)
{
	int width = box.x1 - box.x0;
	for (int y = box.y0; y < box.y1; y++) {
		for (int x = box.x0; x < box.x1; x++) {
			int boxIndex = (y - box.y0) * width + (x - box.x0);
			// The 1 is a margin for float rounding.
			if (g_dabDistances[boxIndex] <= DIRECT_DAB_SKIPPED + 1) {
				continue;
			}
			const float *mixed = g_dabPixels + boxIndex * RGBA_CHANNELS;
			size_t index = (size_t)y * CANVAS_WIDTH + x;
			write_straight_pixel(layer->pixels + index * RGBA_PIXEL_BYTES, mixed);
			if (isSdf) {
				layer->distance[index] = (s8)clamp_float(g_dabDistances[boxIndex], -SDF_FAR, SDF_FAR);
			}
		}
	}
}

// One smudge or smooth dab at (dabX, dabY), strength 0..1. It reads and writes the layer directly.
static IRect apply_direct_dab(float dabX, float dabY, float radius, float strength)
{
	IRect empty = {0, 0, 0, 0};
	Layer *layer = &g_document.layers[g_stroke.layerIndex];
	bool isSdf = layer->props.type == LAYER_SDF;
	bool isSmudge = g_stroke.settings.mode == BRUSH_MODE_SMUDGE;
	if (radius > DIRECT_DAB_RADIUS_MAX) {
		radius = DIRECT_DAB_RADIUS_MAX;
	}
	float moveX = g_stroke.hasLastDab ? dabX - g_stroke.lastDabX : 0;
	float moveY = g_stroke.hasLastDab ? dabY - g_stroke.lastDabY : 0;
	g_stroke.lastDabX = dabX;
	g_stroke.lastDabY = dabY;
	g_stroke.hasLastDab = true;
	IRect box = canvas_box(dabX - radius - 1, dabY - radius - 1, dabX + radius + 1, dabY + radius + 1);
	int width = box.x1 - box.x0;
	int height = box.y1 - box.y0;
	if (width <= 0 || height <= 0 || width > DIRECT_DAB_BOX_MAX || height > DIRECT_DAB_BOX_MAX ||
	    !backup_rect(layer, box)) {
		return empty;
	}
	// Without a move, smudge would pick up the pixel it writes; only its paint changes anything.
	if (isSmudge && moveX == 0 && moveY == 0 && g_stroke.paintRate <= 0) {
		return empty;
	}
	float edgeWidth = DIRECT_DAB_EDGE * radius + 1.0f;
	for (int y = box.y0; y < box.y1; y++) {
		for (int x = box.x0; x < box.x1; x++) {
			int boxIndex = (y - box.y0) * width + (x - box.x0);
			float pixelX = x + 0.5f;
			float pixelY = y + 0.5f;
			float offsetX = pixelX - dabX;
			float offsetY = pixelY - dabY;
			float falloff = (radius - sqrtf(offsetX * offsetX + offsetY * offsetY)) / edgeWidth;
			float weight = clamp_float(falloff, 0, 1) * strength;
			if (weight <= DIRECT_DAB_WEIGHT_MIN) {
				g_dabDistances[boxIndex] = DIRECT_DAB_SKIPPED;
				continue;
			}
			mix_direct_pixel(layer, x, y, boxIndex, pixelX - moveX, pixelY - moveY, weight, isSdf, isSmudge);
		}
	}
	store_direct_dab(layer, box, isSdf);
	layer_grow_bounds(layer, box);
	return box;
}

// A random number 0..1.
static float random_unit(void)
{
	g_stroke.randomState ^= g_stroke.randomState << XORSHIFT_A;
	g_stroke.randomState ^= g_stroke.randomState >> XORSHIFT_B;
	g_stroke.randomState ^= g_stroke.randomState << XORSHIFT_C;
	return (g_stroke.randomState & RANDOM_MASK) * RANDOM_SCALE;
}

// A random number -1..1.
static inline float random_signed(void)
{
	return (random_unit() - 0.5f) * 2;
}

// A hash of the pixel position, 0..1.
static inline float noise_hash(int x, int y)
{
	u32 hash = (u32)x * NOISE_HASH_X + (u32)y * NOISE_HASH_Y;
	hash = (hash ^ (hash >> NOISE_HASH_SHIFT)) * NOISE_HASH_MIX;
	return ((hash ^ (hash >> NOISE_HASH_FINAL_SHIFT)) & 255) * BYTE_TO_UNIT;
}

// The built-in texture at a pixel, 0..1: fine noise mixed with coarser noise.
static inline float noise_texture(int x, int y)
{
	return NOISE_FINE_WEIGHT * noise_hash(x, y) + NOISE_COARSE_WEIGHT * noise_hash(x >> 1, y >> 1);
}

float brush_noise_texture(int x, int y)
{
	return noise_texture(x, y);
}

// Reads the stroke's texture along one canvas row, one pixel after the other from left to right. The tile is
// read in fixed point, so a pixel costs no division.
typedef struct {
	const u8 *tileRow; // NULL: the noise texture
	int y;
	u32 u; // the tile column of the next pixel
	u32 wrap; // the tile's width
} TextureRow;

static void texture_row_start(TextureRow *row, int x, int y)
{
	const TextureTile *tile = g_stroke.settings.texture;
	row->y = y;
	row->tileRow = NULL;
	row->u = 0;
	row->wrap = 0;
	if (!tile) {
		return;
	}
	u32 step = g_stroke.textureStep;
	u32 tileY = (u32)(((u64)y * step) >> TEXTURE_FIXED_SHIFT) % (u32)tile->height;
	row->tileRow = tile->pixels + (size_t)tileY * tile->width;
	row->wrap = (u32)tile->width << TEXTURE_FIXED_SHIFT;
	row->u = (u32)(((u64)x * step) % row->wrap);
}

// The texture at pixel x of the row, 0..1. Call it for every pixel in turn.
static inline float texture_row_next(TextureRow *row, int x)
{
	if (!row->tileRow) {
		return noise_texture(x, row->y);
	}
	float value = row->tileRow[row->u >> TEXTURE_FIXED_SHIFT] * BYTE_TO_UNIT;
	row->u += g_stroke.textureStep;
	while (row->u >= row->wrap) {
		row->u -= row->wrap;
	}
	return value;
}

// A target's sensor reading 0..1: the product of the sensors it uses.
static float sensor_value(u8 sensors, float pressure, float speed, float fade)
{
	float value = 1;
	if (sensors & SENSOR_PRESSURE) {
		value *= pressure < 0 ? 1 : pressure;
	}
	if (sensors & SENSOR_SPEED) {
		value *= 1 - speed;
	}
	if (sensors & SENSOR_FADE) {
		value *= fade;
	}
	return value;
}

// Lifts a value 0..1 so that 0 gives minimumPercent.
static inline float apply_minimum(u8 minimumPercent, float value)
{
	float minimum = minimumPercent * PERCENT_TO_UNIT;
	return minimum + (1 - minimum) * value;
}

// A curve of all zeros is linear.
static bool curve_is_linear(const u8 curve[BRUSH_CURVE_POINTS])
{
	for (int i = 0; i < BRUSH_CURVE_POINTS; i++) {
		if (curve[i]) {
			return false;
		}
	}
	return true;
}

// A target's response curve at a sensor reading 0..1.
static float response_curve(const u8 curve[BRUSH_CURVE_POINTS], float reading)
{
	if (curve_is_linear(curve)) {
		return reading;
	}
	if (!(reading > 0)) {
		return curve[0] * BYTE_TO_UNIT; // also catches NaN
	}
	if (reading >= 1) {
		return curve[CURVE_SEGMENTS] * BYTE_TO_UNIT;
	}
	float position = reading * CURVE_SEGMENTS;
	int segment = (int)position;
	float along = position - segment;
	return (curve[segment] + (curve[segment + 1] - curve[segment]) * along) * BYTE_TO_UNIT;
}

// The SDF shape of a round tip between two pen samples: a capsule whose radius changes along it. One capsule
// per pen sample costs far less than one shape per dab.
static IRect add_capsule_shape(float startX, float startY, float startRadius, float endX, float endY,
                               float endRadius)
{
	IRect empty = {0, 0, 0, 0};
	Layer *layer = &g_document.layers[g_stroke.layerIndex];
	float reach = max_float(startRadius, endRadius) + field_margin(CAPSULE_FIELD_MARGIN);
	IRect box = canvas_box(min_float(startX, endX) - reach, min_float(startY, endY) - reach,
	                       max_float(startX, endX) + reach, max_float(startY, endY) + reach);
	if (box.x0 >= box.x1 || box.y0 >= box.y1 || !backup_rect(layer, box)) {
		return empty;
	}
	float axisX = endX - startX;
	float axisY = endY - startY;
	float lengthSquared = axisX * axisX + axisY * axisY;
	float meltDistance = layer->props.meltRadius * SDF_UNITS_PER_PIXEL;
	for (int y = box.y0; y < box.y1; y++) {
		float pixelY = y + 0.5f;
		for (int x = box.x0; x < box.x1; x++) {
			float pixelX = x + 0.5f;
			// The nearest point of the axis, as a part of its length.
			float along = lengthSquared > CAPSULE_LENGTH_SQUARED_MIN
			                      ? ((pixelX - startX) * axisX + (pixelY - startY) * axisY) / lengthSquared
			                      : 0;
			along = clamp_float(along, 0, 1);
			float offsetX = pixelX - (startX + along * axisX);
			float offsetY = pixelY - (startY + along * axisY);
			float axisDistance = sqrtf(offsetX * offsetX + offsetY * offsetY);
			float radius = startRadius + (endRadius - startRadius) * along;
			float distance = (axisDistance - radius) * SDF_UNITS_PER_PIXEL;
			if (distance >= SDF_FAR) {
				continue;
			}
			size_t index = (size_t)y * CANVAS_WIDTH + x;
			s8 fieldValue = to_field_value(distance);
			if (fieldValue >= g_strokeShape[index]) {
				continue;
			}
			g_strokeShape[index] = fieldValue;
			write_distance(layer, x, y, index, meltDistance);
		}
	}
	layer_grow_bounds(layer, box);
	return box;
}

// How far a point is from the tip's centre, in tip units: 1 on the tip's edge.
static inline float tip_distance(bool isSquare, float tipX, float tipY)
{
	if (isSquare) {
		return max_float(fabsf(tipX), fabsf(tipY));
	}
	return sqrtf(tipX * tipX + tipY * tipY);
}

// How far a point is from the tip's edge in canvas px, negative inside. Tip units alone would shrink it along
// the long axis of a flat tip and widen its outline there. A square tip gets the exact box distance, an
// elliptic one a first-order estimate that is exact on the edge and for a round tip.
static inline float tip_edge_distance(bool isSquare, float tipX, float tipY, float radiusX, float radiusY)
{
	if (isSquare) {
		float outsideX = (fabsf(tipX) - 1) * radiusX;
		float outsideY = (fabsf(tipY) - 1) * radiusY;
		float clampedX = max_float(outsideX, 0);
		float clampedY = max_float(outsideY, 0);
		return sqrtf(clampedX * clampedX + clampedY * clampedY) + min_float(max_float(outsideX, outsideY), 0);
	}
	float unitDistance = sqrtf(tipX * tipX + tipY * tipY);
	float gradientX = tipX / radiusX;
	float gradientY = tipY / radiusY;
	float gradient = sqrtf(gradientX * gradientX + gradientY * gradientY);
	if (gradient < TIP_CENTRE_GRADIENT) {
		return -min_float(radiusX, radiusY);
	}
	return unitDistance * (unitDistance - 1) / gradient;
}

// Builds the stroke's coverage at one pixel up toward the opacity cap. Returns true when it grew.
static inline bool add_coverage(size_t index, float coverage, float opacityCap, float flow)
{
	float current = g_strokeCoverage[index] * BYTE_TO_UNIT;
	if (current < opacityCap) {
		u8 grown = (u8)((current + (opacityCap - current) * coverage * flow) * UNIT_TO_BYTE + 0.5f);
		if (grown > g_strokeCoverage[index]) {
			g_strokeCoverage[index] = grown;
			return true;
		}
	}
	return false;
}

// Unites the tip's shape at one pixel into the stroke's shape. Returns true when it grew.
static inline bool add_tip_shape(size_t index, float distance)
{
	if (distance < SDF_FAR) {
		s8 fieldValue = to_field_value(distance);
		if (fieldValue < g_strokeShape[index]) {
			g_strokeShape[index] = fieldValue;
			return true;
		}
	}
	return false;
}

// Erasing on an SDF layer only cuts the shape; the colour map stays.
static inline bool erases_shape_only(bool isSdf)
{
	return isSdf && g_stroke.settings.erase && g_stroke.settings.mode == BRUSH_MODE_PAINT;
}

// One dab of the preset's tip: where it is, its radii, how it is turned, its edge and how much it lays down.
typedef struct {
	float x;
	float y;
	float radiusX;
	float radiusY;
	float cosAngle;
	float sinAngle;
	bool isSquare;
	float onePixel; // one canvas px along the tip's short axis, in tip units
	float edgeWidth; // the soft edge, in tip units
	float edgeReach; // in tip units: the edge's half pixel of coverage ends here
	float textureStrength;
	float opacityCap;
	float flow;
} DabTip;

static void dab_tip_init(DabTip *tip, float dabX, float dabY, float radius, float angle, float opacityCap,
                         float flow)
{
	const Brush *brush = &g_stroke.settings.brush;
	float ratio = brush->ratio < BRUSH_RATIO_MIN ? TIP_RATIO_MIN : brush->ratio * PERCENT_TO_UNIT;
	tip->x = dabX;
	tip->y = dabY;
	tip->radiusX = radius;
	tip->radiusY = radius * ratio;
	tip->cosAngle = cosf(angle);
	tip->sinAngle = sinf(angle);
	tip->isSquare = brush->tip == TIP_SQUARE;
	tip->onePixel = 1.0f / min_float(tip->radiusX, tip->radiusY);
	tip->edgeWidth = max_float(1 - brush->hardness * PERCENT_TO_UNIT, tip->onePixel);
	tip->edgeReach = 1 + 0.5f * tip->onePixel;
	tip->textureStrength = brush->textureStrength * PERCENT_TO_UNIT;
	tip->opacityCap = opacityCap;
	tip->flow = flow;
}

// Pixel (x, y) in tip units: turned into the tip's frame and scaled by its radii.
static inline void to_tip_units(const DabTip *tip, int x, int y, float *tipX, float *tipY)
{
	float offsetX = x + 0.5f - tip->x;
	float offsetY = y + 0.5f - tip->y;
	*tipX = (offsetX * tip->cosAngle + offsetY * tip->sinAngle) / tip->radiusX;
	*tipY = (-offsetX * tip->sinAngle + offsetY * tip->cosAngle) / tip->radiusY;
}

// The tip's coverage of a pixel, at most 1, with half a pixel of coverage on the edge itself. 0 or less: none.
static inline float tip_coverage(const DabTip *tip, float tipDistance)
{
	float coverage = (1 - tipDistance) / tip->edgeWidth + 0.5f * tip->onePixel / tip->edgeWidth;
	return min_float(coverage, 1);
}

// The texture thins the tip once its edge is worked out.
static inline float textured(const DabTip *tip, float coverage, float textureValue)
{
	return coverage * (1 - tip->textureStrength * (1 - textureValue));
}

// The tip's half width and half height in canvas px once it is turned.
static void tip_extents(const DabTip *tip, float *halfWidth, float *halfHeight)
{
	float cosine = fabsf(tip->cosAngle);
	float sine = fabsf(tip->sinAngle);
	float radiusX = tip->radiusX;
	float radiusY = tip->radiusY;
	if (tip->isSquare) {
		*halfWidth = radiusX * cosine + radiusY * sine;
		*halfHeight = radiusX * sine + radiusY * cosine;
		return;
	}
	*halfWidth = sqrtf(radiusX * radiusX * cosine * cosine + radiusY * radiusY * sine * sine);
	*halfHeight = sqrtf(radiusX * radiusX * sine * sine + radiusY * radiusY * cosine * cosine);
}

// Grows rect, empty while x1 <= x0, by one pixel.
static inline void add_pixel(IRect *rect, int x, int y)
{
	if (rect->x1 <= rect->x0) {
		IRect pixel = {x, y, x + 1, y + 1};
		*rect = pixel;
		return;
	}
	rect->x0 = x < rect->x0 ? x : rect->x0;
	rect->y0 = y < rect->y0 ? y : rect->y0;
	rect->x1 = x >= rect->x1 ? x + 1 : rect->x1;
	rect->y1 = y >= rect->y1 ? y + 1 : rect->y1;
}

// A dab that builds only the stroke's coverage. Its box holds the turned tip and its edge, and it skips the
// pixels it cannot change: those already at the opacity cap, and those outside a round tip. It returns only
// the pixels it changed, so the screen redraws less behind a big brush.
static IRect paint_color_dab(const DabTip *tip, Layer *layer)
{
	IRect empty = {0, 0, 0, 0};
	float halfWidth;
	float halfHeight;
	tip_extents(tip, &halfWidth, &halfHeight);
	float reachX = halfWidth * tip->edgeReach + TIP_EDGE_MARGIN;
	float reachY = halfHeight * tip->edgeReach + TIP_EDGE_MARGIN;
	IRect box = canvas_box(tip->x - reachX, tip->y - reachY, tip->x + reachX, tip->y + reachY);
	if (box.x0 >= box.x1 || box.y0 >= box.y1 || !backup_rect(layer, box)) {
		return empty;
	}
	bool writesColor = !erases_shape_only(layer->props.type == LAYER_SDF);
	u8 fullCoverage = (u8)ceilf(tip->opacityCap * UNIT_TO_BYTE);
	// Past this squared distance a round tip covers nothing, so the square root can wait. The margin keeps
	// rounding from dropping an edge pixel.
	float coveredSquared = tip->edgeReach * tip->edgeReach * TIP_COVERED_SQUARED_MARGIN;
	IRect changed = empty;
	for (int y = box.y0; y < box.y1; y++) {
		TextureRow texture;
		texture_row_start(&texture, box.x0, y);
		for (int x = box.x0; x < box.x1; x++) {
			float textureValue = tip->textureStrength > 0 ? texture_row_next(&texture, x) : 1;
			size_t index = (size_t)y * CANVAS_WIDTH + x;
			if (g_strokeCoverage[index] >= fullCoverage) {
				continue;
			}
			float tipX;
			float tipY;
			to_tip_units(tip, x, y, &tipX, &tipY);
			if (!tip->isSquare && tipX * tipX + tipY * tipY > coveredSquared) {
				continue;
			}
			float coverage = tip_coverage(tip, tip_distance(tip->isSquare, tipX, tipY));
			if (coverage <= 0) {
				continue;
			}
			coverage = textured(tip, coverage, textureValue);
			if (!add_coverage(index, coverage, tip->opacityCap, tip->flow)) {
				continue;
			}
			add_pixel(&changed, x, y);
			if (writesColor) {
				write_color_pixel(layer, x, y, index);
			}
		}
	}
	layer_grow_bounds(layer, changed);
	return changed;
}

// A dab on an SDF layer that also unites its shape into the stroke's shape. Its box is square: the field's
// estimate reaches out past the tip's own box.
static IRect paint_shape_dab(const DabTip *tip, Layer *layer, float radius)
{
	IRect empty = {0, 0, 0, 0};
	float reach = radius * TIP_FIELD_REACH + field_margin(TIP_FIELD_MARGIN);
	IRect box = canvas_box(tip->x - reach, tip->y - reach, tip->x + reach, tip->y + reach);
	if (box.x0 >= box.x1 || box.y0 >= box.y1 || !backup_rect(layer, box)) {
		return empty;
	}
	float meltDistance = layer->props.meltRadius * SDF_UNITS_PER_PIXEL;
	bool writesColor = !erases_shape_only(true);
	for (int y = box.y0; y < box.y1; y++) {
		TextureRow texture;
		texture_row_start(&texture, box.x0, y);
		for (int x = box.x0; x < box.x1; x++) {
			float textureValue = tip->textureStrength > 0 ? texture_row_next(&texture, x) : 1;
			size_t index = (size_t)y * CANVAS_WIDTH + x;
			float tipX;
			float tipY;
			to_tip_units(tip, x, y, &tipX, &tipY);
			float coverage = tip_coverage(tip, tip_distance(tip->isSquare, tipX, tipY));
			if (coverage > 0) {
				coverage = textured(tip, coverage, textureValue);
				if (add_coverage(index, coverage, tip->opacityCap, tip->flow) && writesColor) {
					write_color_pixel(layer, x, y, index);
				}
			}
			float edgeDistance = tip_edge_distance(tip->isSquare, tipX, tipY, tip->radiusX, tip->radiusY);
			if (add_tip_shape(index, edgeDistance * SDF_UNITS_PER_PIXEL)) {
				write_distance(layer, x, y, index, meltDistance);
			}
		}
	}
	layer_grow_bounds(layer, box);
	return box;
}

// One paint or recolour dab of the preset's tip. It builds the stroke's coverage up toward the opacity cap.
// buildsShape: on an SDF layer the dab's shape is also united into the stroke's shape.
static IRect paint_dab(float dabX, float dabY, float radius, float angle, float opacityCap, float flow,
                       bool buildsShape)
{
	Layer *layer = &g_document.layers[g_stroke.layerIndex];
	DabTip tip;
	dab_tip_init(&tip, dabX, dabY, radius, angle, opacityCap, flow);
	bool writesShape =
	        buildsShape && layer->props.type == LAYER_SDF && g_stroke.settings.mode == BRUSH_MODE_PAINT;
	return writesShape ? paint_shape_dab(&tip, layer, radius) : paint_color_dab(&tip, layer);
}

// The first pen sample of a stroke: the path starts here, heading right.
static void start_path(float x, float y, float pressure, u32 timeMs)
{
	g_stroke.hasSample = true;
	g_stroke.lastX = x;
	g_stroke.lastY = y;
	g_stroke.lastPressure = pressure;
	g_stroke.lastTimeMs = timeMs;
	g_stroke.nextDabDistance = 0;
	g_stroke.travelled = 0;
	g_stroke.speed = 0;
	g_stroke.directionX = 1;
	g_stroke.directionY = 0;
	g_stroke.lastCapsuleRadius = 0;
}

// Updates the smoothed pen speed and stroke direction with the pen's latest move.
static void track_motion(float moveX, float moveY, float length, float zoom, float elapsedMs)
{
	g_stroke.speed = g_stroke.speed * SPEED_KEEP + (length * zoom / elapsedMs) * SPEED_TAKE;
	// The new direction is the mean of the old one and this move's.
	float directionX = g_stroke.directionX * 0.5f + moveX / length * 0.5f;
	float directionY = g_stroke.directionY * 0.5f + moveY / length * 0.5f;
	float directionLength = sqrtf(directionX * directionX + directionY * directionY);
	if (directionLength > DIRECTION_LENGTH_MIN) {
		g_stroke.directionX = directionX / directionLength;
		g_stroke.directionY = directionY / directionLength;
	}
}

// The pressure at a point between the previous pen sample and this one. Unknown pressure stays unknown.
static inline float pressure_at(float pressure, float along)
{
	if (g_stroke.lastPressure < 0 || pressure < 0) {
		return pressure;
	}
	return g_stroke.lastPressure + (pressure - g_stroke.lastPressure) * along;
}

// The fade sensor at a distance past the previous pen sample: it grows from 0 to 1 over the preset's fade
// length.
static inline float fade_at(const Brush *brush, float distance, float diameter)
{
	float fade = brush->fade ? (g_stroke.travelled + distance) / (brush->fade * diameter) : 1.0f;
	if (fade > 1) {
		fade = 1;
	}
	return fade;
}

// The radius the size sensors give.
static inline float sensed_radius(const Brush *brush, float pressure, float speed, float fade)
{
	float reading = sensor_value(brush->sizeSensors, pressure, speed, fade);
	return g_stroke.settings.size * apply_minimum(brush->sizeMin, response_curve(brush->sizeCurve, reading));
}

static inline float dab_radius(const Brush *brush, float pressure, float speed, float fade)
{
	float radius = sensed_radius(brush, pressure, speed, fade);
	if (brush->jitter) {
		radius *= 1 - brush->jitter * PERCENT_TO_UNIT * JITTER_SHRINK_MAX * random_unit();
	}
	if (radius < DAB_RADIUS_MIN) {
		radius = DAB_RADIUS_MIN;
	}
	return radius;
}

static inline void scatter_dab(const Brush *brush, float radius, float *dabX, float *dabY)
{
	float scatterReach = brush->scatter * PERCENT_TO_UNIT * 2 * radius;
	*dabX += random_signed() * scatterReach;
	*dabY += random_signed() * scatterReach;
}

// Draws one dab with the opacity and flow its sensors give.
static IRect draw_dab(float dabX, float dabY, float radius, float pressure, float speed, float fade,
                      bool isDirect, bool buildsShape)
{
	const Brush *brush = &g_stroke.settings.brush;
	float opacityReading = sensor_value(brush->opacitySensors, pressure, speed, fade);
	float sensedOpacity =
	        apply_minimum(brush->opacityMin, response_curve(brush->opacityCurve, opacityReading));
	if (isDirect) {
		return apply_direct_dab(dabX, dabY, radius, g_stroke.settings.strength * sensedOpacity);
	}
	float opacityCap = brush->opacity * PERCENT_TO_UNIT * g_stroke.settings.opacity * sensedOpacity;
	float flowReading = sensor_value(brush->flowSensors, pressure, speed, fade);
	float flow = brush->flow * PERCENT_TO_UNIT * response_curve(brush->flowCurve, flowReading);
	float presetAngle = brush->angle * DEGREES_TO_RADIANS;
	float angle = presetAngle + (brush->follow ? atan2f(g_stroke.directionY, g_stroke.directionX) : 0);
	float cappedOpacity = opacityCap > 1 ? 1 : opacityCap;
	return paint_dab(dabX, dabY, radius, angle, cappedOpacity, flow, buildsShape);
}

// Adds the SDF capsule from the previous pen sample to this one.
static void add_capsule(IRect *changed, float x, float y, float pressure, float speed, float length)
{
	const Brush *brush = &g_stroke.settings.brush;
	float diameter = 2 * g_stroke.settings.size;
	float fade = fade_at(brush, length, diameter);
	float endRadius = sensed_radius(brush, pressure, speed, fade);
	if (endRadius < DAB_RADIUS_MIN) {
		endRadius = DAB_RADIUS_MIN;
	}
	float startX = g_stroke.lastX;
	float startY = g_stroke.lastY;
	float startRadius = g_stroke.lastCapsuleRadius > 0 ? g_stroke.lastCapsuleRadius : endRadius;
	rect_union(changed, add_capsule_shape(startX, startY, startRadius, x, y, endRadius));
	g_stroke.lastCapsuleRadius = endRadius;
}

// Lays the dabs along the move from the previous pen sample, one spacing apart. buildsShape: on an SDF layer
// each dab also adds its shape.
static void lay_dabs_along_move(IRect *changed, float moveX, float moveY, float length, float pressure,
                                float speed, bool isDirect, bool buildsShape)
{
	const Brush *brush = &g_stroke.settings.brush;
	float diameter = 2 * g_stroke.settings.size;
	float position = g_stroke.nextDabDistance;
	while (position <= length) {
		float along = length > 0 ? position / length : 1.0f;
		float dabPressure = pressure_at(pressure, along);
		float fade = fade_at(brush, position, diameter);
		float radius = dab_radius(brush, dabPressure, speed, fade);
		float dabX = g_stroke.lastX + moveX * along;
		float dabY = g_stroke.lastY + moveY * along;
		if (brush->scatter) {
			scatter_dab(brush, radius, &dabX, &dabY);
		}
		rect_union(changed, draw_dab(dabX, dabY, radius, dabPressure, speed, fade, isDirect, buildsShape));
		float step = isDirect ? radius * DIRECT_DAB_SPACING : brush->spacing * PERCENT_TO_UNIT * 2 * radius;
		position += step < DAB_STEP_MIN ? DAB_STEP_MIN : step;
	}
	g_stroke.nextDabDistance = position - length;
}

IRect brush_stroke_add(float x, float y, float pressure, u32 timeMs)
{
	IRect changed = {0, 0, 0, 0};
	if (!g_stroke.active || g_stroke.settings.brush.spacing == 0) {
		return changed;
	}
	if (!(pressure >= 0)) {
		pressure = PRESSURE_UNKNOWN; // also catches NaN
	} else if (pressure > 1) {
		pressure = 1;
	}
	const Brush *brush = &g_stroke.settings.brush;
	Layer *layer = &g_document.layers[g_stroke.layerIndex];
	bool isDirect =
	        g_stroke.settings.mode == BRUSH_MODE_SMUDGE || g_stroke.settings.mode == BRUSH_MODE_SMOOTH;
	bool paintsShape = layer->props.type == LAYER_SDF && g_stroke.settings.mode == BRUSH_MODE_PAINT;
	// A plain round tip draws its shape as one capsule per pen sample instead of one shape per dab.
	bool usesCapsule = paintsShape && brush->tip == TIP_ROUND && brush->ratio >= 100 && brush->scatter == 0;
	float zoom = g_stroke.settings.zoom > 0 ? g_stroke.settings.zoom : 1.0f;
	if (!g_stroke.hasSample) {
		start_path(x, y, pressure, timeMs);
	}
	float moveX = x - g_stroke.lastX;
	float moveY = y - g_stroke.lastY;
	float length = sqrtf(moveX * moveX + moveY * moveY);
	float elapsedMs = timeMs > g_stroke.lastTimeMs ? (float)(timeMs - g_stroke.lastTimeMs) : 1.0f;
	if (length > PEN_MOVE_MIN) {
		track_motion(moveX, moveY, length, zoom, elapsedMs);
	}
	float speed = g_stroke.speed / SPEED_FULL;
	if (speed > 1) {
		speed = 1;
	}
	lay_dabs_along_move(&changed, moveX, moveY, length, pressure, speed, isDirect,
	                    paintsShape && !usesCapsule);
	if (usesCapsule) {
		add_capsule(&changed, x, y, pressure, speed, length);
	}
	g_stroke.travelled += length;
	g_stroke.lastX = x;
	g_stroke.lastY = y;
	g_stroke.lastPressure = pressure;
	g_stroke.lastTimeMs = timeMs;
	return changed;
}

// Clears the scratch buffers in the tiles the stroke touched.
static void clear_scratch(const UndoStep *step)
{
	for (int i = 0; i < undo_step_tile_count(step); i++) {
		int tile = undo_step_tile_index(step, i);
		int tileX = tile % UNDO_TILE_COLUMNS;
		int tileY = tile / UNDO_TILE_COLUMNS;
		for (int row = 0; row < UNDO_TILE_SIZE; row++) {
			size_t rowStart = (size_t)(tileY * UNDO_TILE_SIZE + row) * CANVAS_WIDTH + tileX * UNDO_TILE_SIZE;
			memset(g_strokeCoverage + rowStart, 0, UNDO_TILE_SIZE);
			memset(g_strokeShape + rowStart, SDF_FAR, UNDO_TILE_SIZE);
		}
	}
}

// keepUndo: the stroke's undo step goes on the undo stack; otherwise it is dropped.
static void end_stroke(bool keepUndo)
{
	if (!g_stroke.active) {
		return;
	}
	UndoStep *step = g_stroke.undoStep;
	clear_scratch(step);
	g_stroke.active = false;
	g_stroke.undoStep = NULL;
	if (!undo_step_tile_count(step) || !keepUndo) {
		undo_step_free(step);
		return;
	}
	undo_push(step);
}

void brush_stroke_end(void)
{
	end_stroke(true);
}

void brush_stroke_end_without_undo(void)
{
	end_stroke(false);
}
