// The brush engine. While the pen is down, paint dabs build up in scratch buffers: coverage for the colour
// map and, on an SDF layer, a distance field for the shape. The layer is recomputed from its pre-stroke state
// plus that scratch. Smudge and smooth dabs change the layer directly; a smudge brush with paint also lays
// down the stroke's colour as it smudges (wet paint). The pre-stroke state becomes an undo step.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "document.h"

enum {
	BRUSH_MODE_PAINT,
	BRUSH_MODE_RECOLOR,
	BRUSH_MODE_SMUDGE,
	BRUSH_MODE_SMOOTH,
	BRUSH_MODE_COUNT
};

enum {
	TIP_ROUND,
	TIP_SQUARE,
	TIP_COUNT
};

// Sensor flags. Several sensors on one target multiply.
enum {
	SENSOR_PRESSURE = 1,
	SENSOR_SPEED = 2,
	SENSOR_FADE = 4
};

#define BRUSH_RADIUS_MIN 1 // canvas px
#define BRUSH_RADIUS_MAX 60
#define BRUSH_RATIO_MIN 10 // percent; a flatter tip is drawn at this ratio
#define BRUSH_ANGLE_MAX 179 // degrees; a tip turned by 180 looks the same
#define BRUSH_FADE_MAX 60 // the longest fade sensor, in dab diameters
#define BRUSH_FADE_DEFAULT 6 // a fade sensor without a length of its own
#define BRUSH_NAME_SIZE 32 // with the terminating zero; Krita names run to about 25 characters
#define BRUSH_CURVE_POINTS 5 // a response curve's values at the sensor readings 0, 0.25, 0.5, 0.75 and 1
#define BRUSH_TEXTURE_NAME_SIZE 40 // with the terminating zero: a texture's file name (textures.h)
#define BRUSH_TEXTURE_SCALE_MIN 10 // percent
#define BRUSH_TEXTURE_SCALE_MAX 250
#define BRUSH_TEXTURE_SCALE_DEFAULT 100

// A brush preset: a tip, how its dabs are laid down, and which sensors drive what.
typedef struct {
	char name[BRUSH_NAME_SIZE];
	u8 tip; // TIP_*
	u8 hardness; // 0..100: 100 = crisp edge, 0 = faded from the centre
	u8 ratio; // BRUSH_RATIO_MIN..100: tip height as % of its width (100 = round)
	u8 angle; // 0..BRUSH_ANGLE_MAX degrees
	u8 follow; // 1: the tip also turns with the stroke direction
	u8 spacing; // 1..100: % of the dab diameter between two dabs
	u8 flow; // 1..100: how much one dab lays down (dabs build up to the opacity cap)
	u8 opacity; // 1..100: the most a stroke can reach
	u8 scatter; // 0..100: random offset of each dab, % of its diameter
	u8 jitter; // 0..100: random size variation
	u8 textureStrength; // 0..100: how much the texture breaks the tip up (chalk, paper)
	u8 sizeSensors; // SENSOR_* flags that drive the size
	u8 opacitySensors; // SENSOR_* flags that drive the opacity cap
	u8 flowSensors; // SENSOR_* flags that drive the flow
	u8 sizeMin; // % of the size left when the sensors read 0 (a light touch)
	u8 opacityMin; // % of the opacity cap left when the sensors read 0
	u8 fade; // length of the fade sensor in dab diameters (0 = it always reads 1)
	u8 paint; // 0..100: smudge mode: how much of the stroke's colour each dab mixes into what it picks up
	u8 textureScale; // BRUSH_TEXTURE_SCALE_MIN..MAX percent; 0 reads as BRUSH_TEXTURE_SCALE_DEFAULT
	// Response curves: sensor reading to 0..255. All zeros means linear.
	u8 sizeCurve[BRUSH_CURVE_POINTS];
	u8 opacityCurve[BRUSH_CURVE_POINTS];
	u8 flowCurve[BRUSH_CURVE_POINTS];
	char texture[BRUSH_TEXTURE_NAME_SIZE]; // the texture's file name; "" = the built-in noise
} Brush;

// A grey texture tile, repeated over the canvas: 0 (black) lets no paint through, 255 (white) all of it.
typedef struct {
	const u8 *pixels;
	int width;
	int height;
} TextureTile;

// The settings of one stroke.
typedef struct {
	u8 mode; // BRUSH_MODE_*
	bool erase; // BRUSH_MODE_PAINT: remove instead of add
	u8 color[3];
	float opacity; // 0..1, multiplies the preset's opacity
	float strength; // 0..1: smudge / smooth amount
	Brush brush; // a stroke with brush.spacing 0 lays no dabs
	const TextureTile *texture; // brush.texture's tile; NULL: the noise
	float size; // base radius in canvas px
	float zoom; // screen px per canvas px, for the speed sensor (0 = 1)
	// SDF: the field stops near the edge. Only for a layer that never gets an outline, like the brush preview's.
	bool shortField;
} StrokeSettings;

bool brush_engine_init(void); // allocates the scratch buffers
// Starts a stroke on the current layer. False when a stroke is running or out of memory.
bool brush_stroke_begin(const StrokeSettings *settings);
// Adds a pen sample and lays down the dabs up to it. pressure is 0..1 (< 0: unknown, read as full); timeMs is
// a monotonic time. Returns the changed canvas rect.
IRect brush_stroke_add(float x, float y, float pressure, u32 timeMs);
void brush_stroke_end(void); // the stroke's changes become one undo step
void brush_stroke_end_without_undo(void);
bool brush_stroke_active(void);
float brush_noise_texture(int x, int y); // the built-in noise texture at a canvas pixel, 0..1
