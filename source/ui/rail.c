#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rail.h"
#include "app_state.h"
#include "brush_library.h"
#include "color.h"
#include "draw.h"
#include "history.h"
#include "icons.h"
#include "math_util.h"
#include "page_view.h"
#include "tools.h"
#include "ui.h"
#include "undo.h"
#include "widgets.h"

#define SIZE_RING_FRAMES 45 // how long the brush size ring shows after a size change
#define TOOL_TOAST_PREFIX_SIZE 16 // "Eraser: "

// Rail layout in px, x from the rail's left edge.
#define RAIL_EDGE_COLOR RGB(10, 10, 12)
#define RAIL_CENTER_X (RAIL_WIDTH / 2)
#define LEFT_COLUMN_X 2
#define RIGHT_COLUMN_X 25
#define FULL_ROW_WIDTH 44 // both columns
#define TOOL_BUTTON_WIDTH 21
#define TOOL_BUTTON_HEIGHT 28
#define TOOL_ROW_Y 2
#define TOOL_ROW_STEP 30

// The brush size slider: a vertical track, bigger towards the top.
#define SIZE_SLIDER_Y 64
#define SIZE_SLIDER_HEIGHT 70
#define SIZE_TRACK_Y (SIZE_SLIDER_Y + 10)
#define SIZE_TRACK_HEIGHT 42
#define SIZE_TRACK_WIDTH 2
#define SIZE_KNOB_MIN_RADIUS 2.5f
#define SIZE_KNOB_GROWTH 6.5f // added to the radius at the largest size
#define SIZE_KNOB_OUTLINE 1.5f
#define SIZE_LABEL_Y (SIZE_SLIDER_Y + 54)

#define SWATCH_X 4
#define SWATCH_Y 140
#define SWATCH_WIDTH 40
#define SWATCH_HEIGHT 26
#define RECENT_SWATCH_Y 172
#define RECENT_SWATCH_SIZE 12
#define RECENT_SWATCH_STEP 14
#define RECENT_SWATCHES_SHOWN 3

#define UNDO_Y 190
#define UNDO_HEIGHT 22
#define BOTTOM_ROW_Y 215
#define BOTTOM_BUTTON_HEIGHT 23
#define RAIL_EDGE_WIDTH 1

static const char *const TOOL_NAMES[] = {"Brush", "Eraser", "Eyedropper", "Move"};

static int g_sizeRingFrames; // frames left to show the brush size ring

static int rail_x(void)
{
	return g_leftHanded ? BOTTOM_SCREEN_WIDTH - RAIL_WIDTH : 0;
}

// Names the tool with the preset it holds.
static void toast_tool(void)
{
	if (g_tool != TOOL_BRUSH && g_tool != TOOL_ERASER) {
		app_toast("%s", TOOL_NAMES[g_tool]);
		return;
	}
	char before[TOOL_TOAST_PREFIX_SIZE];
	snprintf(before, sizeof before, "%s: ", TOOL_NAMES[g_tool]);
	app_toast_name(before, g_presetIndex >= 0 ? g_library[g_presetIndex].brush.name : "own brush", "");
}

void rail_toggle_eraser(void)
{
	tool_select(g_tool == TOOL_ERASER ? TOOL_BRUSH : TOOL_ERASER);
	toast_tool();
}

void rail_set_brush_size(int radius)
{
	if (radius < BRUSH_RADIUS_MIN) {
		radius = BRUSH_RADIUS_MIN;
	} else if (radius > BRUSH_RADIUS_MAX) {
		radius = BRUSH_RADIUS_MAX;
	}
	g_brushRadius = radius;
	g_sizeRingFrames = SIZE_RING_FRAMES;
	app_request_bottom_redraw();
}

void rail_tick(void)
{
	if (g_sizeRingFrames <= 0) {
		return;
	}
	g_sizeRingFrames--;
	if (g_sizeRingFrames == 0) {
		app_request_bottom_redraw();
	}
}

bool rail_size_ring_shown(void)
{
	return g_sizeRingFrames > 0;
}

static int button_style(bool on)
{
	return on ? BUTTON_STYLE_ON : BUTTON_STYLE_NORMAL;
}

static void tool_button(int x, int y, int tool, int icon)
{
	if (!widget_button(x, y, TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT, icon, NULL,
	                   button_style(g_tool == tool))) {
		return;
	}
	if (g_tool == tool && (tool == TOOL_BRUSH || tool == TOOL_ERASER)) {
		ui_toggle_sheet(SHEET_BRUSH_LIBRARY); // a second tap opens its settings
		return;
	}
	tool_select(tool);
	if (ui_current_sheet()) {
		ui_open_sheet(SHEET_NONE);
	}
	toast_tool();
}

static void tool_buttons(int railX)
{
	int leftX = railX + LEFT_COLUMN_X;
	int rightX = railX + RIGHT_COLUMN_X;
	int secondRowY = TOOL_ROW_Y + TOOL_ROW_STEP;
	tool_button(leftX, TOOL_ROW_Y, TOOL_BRUSH, IC_BRUSH);
	tool_button(rightX, TOOL_ROW_Y, TOOL_ERASER, IC_ERASER);
	tool_button(leftX, secondRowY, TOOL_EYEDROPPER, IC_PICKER);
	tool_button(rightX, secondRowY, TOOL_MOVE, IC_MOVE);
}

// The slider position grows with the square root of the radius, for finer steps at small sizes.
static void size_slider(int railX)
{
	int sliderX = railX + LEFT_COLUMN_X;
	int centerX = railX + RAIL_CENTER_X;
	if (!g_uiSurface) {
		if (widget_tapped(sliderX, SIZE_SLIDER_Y, FULL_ROW_WIDTH, SIZE_SLIDER_HEIGHT)) {
			widget_capture(WIDGET_SIZE_SLIDER);
		}
		if (widget_captured(WIDGET_SIZE_SLIDER) && g_touch.held) {
			float position = clamp_unit(1 - (g_touch.y - SIZE_TRACK_Y) / (float)SIZE_TRACK_HEIGHT);
			int radiusRange = BRUSH_RADIUS_MAX - BRUSH_RADIUS_MIN;
			rail_set_brush_size(BRUSH_RADIUS_MIN + (int)(position * position * radiusRange + 0.5f));
		}
		return;
	}
	draw_rounded_rect(g_uiSurface, sliderX, SIZE_SLIDER_Y, FULL_ROW_WIDTH, SIZE_SLIDER_HEIGHT, COLOR_WIDGET);
	draw_fill(g_uiSurface, centerX - SIZE_TRACK_WIDTH / 2, SIZE_TRACK_Y, SIZE_TRACK_WIDTH, SIZE_TRACK_HEIGHT,
	          COLOR_WIDGET_LIGHT, 255);
	float position = sqrtf((g_brushRadius - BRUSH_RADIUS_MIN) / (float)(BRUSH_RADIUS_MAX - BRUSH_RADIUS_MIN));
	float knobY = SIZE_TRACK_Y + (1 - position) * SIZE_TRACK_HEIGHT;
	float knobRadius = SIZE_KNOB_MIN_RADIUS + position * SIZE_KNOB_GROWTH;
	draw_disc(g_uiSurface, centerX, knobY, knobRadius + SIZE_KNOB_OUTLINE, COLOR_RAIL);
	draw_disc(g_uiSurface, centerX, knobY, knobRadius,
	          widget_captured(WIDGET_SIZE_SLIDER) ? COLOR_ACCENT : COLOR_TEXT);
	draw_text(g_uiSurface, FONT_SMALL, centerX, SIZE_LABEL_Y, COLOR_DIM, ALIGN_CENTER, "%d", g_brushRadius);
}

// The colour, then the three most recent other colours.
static void color_swatches(int railX)
{
	int swatchX = railX + SWATCH_X;
	if (widget_swatch(swatchX, SWATCH_Y, SWATCH_WIDTH, SWATCH_HEIGHT, g_color,
	                  ui_current_sheet() == SHEET_COLOR)) {
		ui_toggle_sheet(SHEET_COLOR);
	}
	int shown = 0;
	for (int i = 0; i < color_recent_count() && shown < RECENT_SWATCHES_SHOWN; i++) {
		if (!memcmp(color_recent(i), g_color, sizeof g_color)) {
			continue;
		}
		int recentX = swatchX + shown * RECENT_SWATCH_STEP;
		if (widget_swatch(recentX, RECENT_SWATCH_Y, RECENT_SWATCH_SIZE, RECENT_SWATCH_SIZE, color_recent(i),
		                  false)) {
			color_set(color_recent(i));
		}
		shown++;
	}
}

static void bottom_buttons(int railX)
{
	int leftX = railX + LEFT_COLUMN_X;
	int rightX = railX + RIGHT_COLUMN_X;
	int undoStyle = undo_count() ? BUTTON_STYLE_NORMAL : BUTTON_STYLE_DIM;
	if (widget_button(leftX, UNDO_Y, TOOL_BUTTON_WIDTH, UNDO_HEIGHT, IC_UNDO, NULL, undoStyle)) {
		history_undo();
	}
	int redoStyle = undo_redo_count() ? BUTTON_STYLE_NORMAL : BUTTON_STYLE_DIM;
	if (widget_button(rightX, UNDO_Y, TOOL_BUTTON_WIDTH, UNDO_HEIGHT, IC_REDO, NULL, redoStyle)) {
		history_redo();
	}
	bool layersOpen = ui_root_sheet(ui_current_sheet()) == SHEET_LAYERS;
	if (widget_button(leftX, BOTTOM_ROW_Y, TOOL_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT, IC_LAYERS, NULL,
	                  button_style(layersOpen))) {
		ui_toggle_sheet(SHEET_LAYERS);
	}
	bool menuOpen = ui_root_sheet(ui_current_sheet()) == SHEET_MENU;
	if (widget_button(rightX, BOTTOM_ROW_Y, TOOL_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT, IC_MENU, NULL,
	                  button_style(menuOpen))) {
		ui_toggle_sheet(SHEET_MENU);
	}
}

void rail_run(void)
{
	int railX = rail_x();
	if (g_uiSurface) {
		draw_fill(g_uiSurface, railX, 0, RAIL_WIDTH, SCREEN_HEIGHT, COLOR_RAIL, 255);
		int edgeX = g_leftHanded ? railX : railX + RAIL_WIDTH - RAIL_EDGE_WIDTH; // the side facing the page
		draw_fill(g_uiSurface, edgeX, 0, RAIL_EDGE_WIDTH, SCREEN_HEIGHT, RAIL_EDGE_COLOR, 255);
	}
	tool_buttons(railX);
	size_slider(railX);
	color_swatches(railX);
	bottom_buttons(railX);
	if (!g_uiSurface && widget_touch_in(railX, 0, RAIL_WIDTH, SCREEN_HEIGHT)) {
		widget_take_press(); // gaps between the buttons are not canvas
	}
}
