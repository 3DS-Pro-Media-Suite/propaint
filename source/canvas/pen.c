#include <string.h>
#include "pen.h"
#include "app_state.h"
#include "brush_engine.h"
#include "color.h"
#include "color_units.h"
#include "document.h"
#include "mascot.h"
#include "page_view.h"
#include "pressure.h"
#include "render.h"
#include "textures.h"

#define PENDING_POINTS_MAX 64 // points held while the pressure settles
#define PENDING_POINT_SPACING_MS 4 // the held points are timed as if sampled this far apart
#define DEFAULT_DAB_SPACING 8 // the dab engine is used only when the brush has a spacing
#define MIN_MOVE 0.3f // canvas px a point must be from the last one to be taken
#define MIN_MOVE_SQUARED (MIN_MOVE * MIN_MOVE)

typedef struct {
	float x;
	float y;
} CanvasPoint;

// How far each new point moves toward the stylus, per stroke smoothing level.
static const float SMOOTHING_FOLLOW[STROKE_SMOOTHING_LEVELS] = {1.0f, 0.6f, 0.3f};

int g_stylusX;
int g_stylusY;

static float g_lastCanvasX; // the stroke's last point, after smoothing
static float g_lastCanvasY;

static StrokeSettings g_strokeBrush;
static bool g_strokePending; // the stroke waits for the pressure to settle
static CanvasPoint g_pendingPoints[PENDING_POINTS_MAX];
static int g_pendingCount;

// Begins the stroke and lays down the points held while the pressure settled.
static void lay_down_pending_points(float pressure)
{
	g_strokePending = false;
	if (!brush_stroke_begin(&g_strokeBrush)) {
		g_penDown = false;
		return;
	}
	bool laysColor = g_strokeBrush.mode == BRUSH_MODE_PAINT || g_strokeBrush.mode == BRUSH_MODE_RECOLOR;
	if (!g_strokeBrush.erase && laysColor) {
		color_push_recent(g_color);
	}
	u32 now = (u32)osGetTime();
	for (int i = 0; i < g_pendingCount; i++) {
		u32 age = (u32)(g_pendingCount - 1 - i) * PENDING_POINT_SPACING_MS;
		CanvasPoint point = g_pendingPoints[i];
		page_view_mark_dirty(brush_stroke_add(point.x, point.y, pressure, now - age));
	}
	g_unsaved = true;
}

// Holds a point while the stroke waits; points beyond PENDING_POINTS_MAX are dropped.
static void hold_point(float x, float y)
{
	if (g_pendingCount >= PENDING_POINTS_MAX) {
		return;
	}
	g_pendingPoints[g_pendingCount].x = x;
	g_pendingPoints[g_pendingCount].y = y;
	g_pendingCount++;
	g_lastCanvasX = x;
	g_lastCanvasY = y;
}

void pen_down(int screenX, int screenY)
{
	if (g_document.current >= g_document.layerCount) {
		return;
	}
	Layer *layer = document_current_layer();
	if (!layer->props.visible || layer->props.locked) {
		mascot_event(MASCOT_REFUSED);
		app_toast(layer->props.locked ? "This layer is locked" : "This layer is hidden");
		return;
	}
	bool erase = g_tool == TOOL_ERASER;
	StrokeSettings brush = {
	        .mode = erase ? BRUSH_MODE_PAINT : (u8)g_brushMode,
	        .erase = erase,
	        .color = {g_color[0], g_color[1], g_color[2]},
	        .opacity = 1.0f,
	        .strength = g_smudgeStrength * PERCENT_TO_UNIT,
	        .brush = g_brush,
	        .texture = textures_tile(g_brush.texture),
	        .size = (float)g_brushRadius,
	        .zoom = g_view.zoom,
	};
	if (!brush.brush.spacing) {
		brush.brush.spacing = DEFAULT_DAB_SPACING;
	}
	g_strokeBrush = brush;
	g_penDown = true;
	g_stylusX = screenX;
	g_stylusY = screenY;
	page_view_screen_to_canvas(screenX, screenY, &g_lastCanvasX, &g_lastCanvasY);
	g_pendingPoints[0].x = g_lastCanvasX;
	g_pendingPoints[0].y = g_lastCanvasY;
	g_pendingCount = 1;
	pressure_stroke_begin();
	if (pressure_settled()) {
		lay_down_pending_points(pressure_current());
	} else {
		g_strokePending = true;
	}
}

void pen_move(int screenX, int screenY)
{
	if (!g_penDown) {
		return;
	}
	g_stylusX = screenX;
	g_stylusY = screenY;
	float canvasX;
	float canvasY;
	page_view_screen_to_canvas(screenX, screenY, &canvasX, &canvasY);
	float follow = SMOOTHING_FOLLOW[g_strokeSmoothing];
	float nextX = g_lastCanvasX + (canvasX - g_lastCanvasX) * follow;
	float nextY = g_lastCanvasY + (canvasY - g_lastCanvasY) * follow;
	float dx = nextX - g_lastCanvasX;
	float dy = nextY - g_lastCanvasY;
	bool moved = dx * dx + dy * dy >= MIN_MOVE_SQUARED;
	if (g_strokePending) {
		if (moved) {
			hold_point(nextX, nextY);
		}
		if (pressure_settled()) {
			lay_down_pending_points(pressure_current());
		}
		return;
	}
	if (!moved) {
		return;
	}
	page_view_mark_dirty(brush_stroke_add(nextX, nextY, pressure_current(), (u32)osGetTime()));
	g_lastCanvasX = nextX;
	g_lastCanvasY = nextY;
}

void pen_up(void)
{
	if (!g_penDown) {
		return;
	}
	pressure_stroke_end();
	if (g_strokePending) {
		// A tap shorter than the pressure warm-up still paints.
		lay_down_pending_points(pressure_current());
	}
	if (!g_penDown) {
		// The stroke could not begin.
		return;
	}
	brush_stroke_end();
	g_penDown = false;
	mascot_event(MASCOT_STROKE);
	page_view_redraw_top_rect();
}

void pen_pick_color(int screenX, int screenY)
{
	float canvasX;
	float canvasY;
	page_view_screen_to_canvas(screenX, screenY, &canvasX, &canvasY);
	if (canvasX < 0 || canvasY < 0 || canvasX >= CANVAS_WIDTH || canvasY >= CANVAS_HEIGHT) {
		return;
	}
	// One pixel of the composited page at zoom 1, in framebuffer order (BGR).
	View pixelView = {.centerX = canvasX, .centerY = canvasY, .zoom = 1};
	u8 bgr[3];
	render(&pixelView, bgr, 1, 1, 0, 0, 1, 1);
	u8 rgb[3] = {bgr[2], bgr[1], bgr[0]};
	color_set_keep_tool(rgb);
	g_stylusX = screenX;
	g_stylusY = screenY;
}
