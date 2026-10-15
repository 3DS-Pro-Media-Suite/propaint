#include <string.h>
#include "color_sheet.h"
#include "color.h"
#include "draw.h"
#include "math_util.h"
#include "ui.h"
#include "widgets.h"

#define HUE_BAR_MAX 359.0f // the hue at the bottom of the hue bar

// Colour sheet layout.
#define PICKER_TOP 8 // the square, the hue bar and the new swatch
#define SQUARE_SIZE 150 // saturation across, value down
#define HUE_BAR_X 164
#define HUE_BAR_WIDTH 20
#define TOUCH_MARGIN 4 // the square and the bar take presses this far outside
#define KNOB_OUTER_RADIUS 5
#define KNOB_INNER_RADIUS 4
#define MARKER_HEIGHT 1 // the hue marker: a line across the bar, outlined white and then black
#define MARKER_OUTLINE 1
#define SWATCH_COLUMN_X 192 // the new and old swatches, the hex code and the palette
#define SWATCH_COLUMN_WIDTH 72
#define NEW_SWATCH_HEIGHT 30
#define NEW_LABEL_Y 15
#define OLD_SWATCH_Y (PICKER_TOP + NEW_SWATCH_HEIGHT)
#define OLD_SWATCH_HEIGHT 16
#define HEX_CODE_Y 57
#define PALETTE_SIZE 12
#define PALETTE_COLUMNS 3
#define PALETTE_TOP 78
#define PALETTE_STEP 25
#define SWATCH_SIZE 22
#define RECENT_LABEL_Y 164
#define RECENT_TOP 182
#define RECENT_STEP 26
#define HINT_Y 216

// Black, grey and white, then colours around the hue circle.
static const u8 PALETTE[PALETTE_SIZE][3] = {
        {0, 0, 0},      {90, 90, 96},  {255, 255, 255}, {214, 48, 49},  {232, 126, 36}, {250, 205, 60},
        {120, 190, 70}, {40, 150, 90}, {40, 170, 190},  {50, 110, 220}, {120, 70, 190}, {230, 100, 160},
};

static u8 g_colorBefore[3]; // colour when the colour sheet opened

void color_sheet_opened(void)
{
	memcpy(g_colorBefore, g_color, sizeof g_colorBefore);
}

// The hue a press on the hue bar picks: 0 at the top row, HUE_BAR_MAX at the bottom row.
static float hue_at_row(int row)
{
	return clamp_unit(row / (float)(SQUARE_SIZE - 1)) * HUE_BAR_MAX;
}

// The stylus drags the saturation / value knob in the square or the marker on the hue bar.
static void picker_input(int squareX, int hueBarX)
{
	int grabSize = SQUARE_SIZE + 2 * TOUCH_MARGIN;
	if (widget_tapped(squareX - TOUCH_MARGIN, PICKER_TOP - TOUCH_MARGIN, grabSize, grabSize)) {
		widget_capture(WIDGET_COLOR_SQUARE);
	}
	if (widget_tapped(hueBarX - TOUCH_MARGIN, PICKER_TOP - TOUCH_MARGIN, HUE_BAR_WIDTH + 2 * TOUCH_MARGIN,
	                  grabSize)) {
		widget_capture(WIDGET_HUE_BAR);
	}
	if (!g_touch.held) {
		return;
	}
	if (widget_captured(WIDGET_COLOR_SQUARE)) {
		g_pickerSaturation = clamp_unit((g_touch.x - squareX) / (float)(SQUARE_SIZE - 1));
		g_pickerValue = 1 - clamp_unit((g_touch.y - PICKER_TOP) / (float)(SQUARE_SIZE - 1));
	} else if (widget_captured(WIDGET_HUE_BAR)) {
		g_pickerHue = hue_at_row(g_touch.y - PICKER_TOP);
	} else {
		return;
	}
	color_set_from_picker();
}

static void draw_picker(int squareX, int hueBarX)
{
	u8 pureHue[3];
	color_hsv_to_rgb(g_pickerHue, 1, 1, pureHue);
	for (int x = 0; x < SQUARE_SIZE; x++) {
		float saturation = x / (float)(SQUARE_SIZE - 1);
		for (int y = 0; y < SQUARE_SIZE; y++) {
			float value = 1.0f - y / (float)(SQUARE_SIZE - 1);
			u8 rgb[3];
			color_square_rgb(pureHue, saturation, value, rgb);
			draw_pixel(g_uiSurface, squareX + x, PICKER_TOP + y, RGB(rgb[0], rgb[1], rgb[2]), 255);
		}
	}
	for (int y = 0; y < SQUARE_SIZE; y++) {
		u8 rgb[3];
		// Drawn in steps of HUE_BAR_MAX / SQUARE_SIZE, so the bar keeps its colours; its bottom row ends a
		// step short of HUE_BAR_MAX.
		color_hsv_to_rgb(y / (float)SQUARE_SIZE * HUE_BAR_MAX, 1, 1, rgb);
		draw_fill(g_uiSurface, hueBarX, PICKER_TOP + y, HUE_BAR_WIDTH, 1, draw_color(rgb), 255);
	}
	float knobX = squareX + g_pickerSaturation * (SQUARE_SIZE - 1);
	float knobY = PICKER_TOP + (1 - g_pickerValue) * (SQUARE_SIZE - 1);
	draw_ring(g_uiSurface, knobX, knobY, KNOB_OUTER_RADIUS, COLOR_BLACK, 255);
	draw_ring(g_uiSurface, knobX, knobY, KNOB_INNER_RADIUS, COLOR_WHITE, 255);
	int markerY = PICKER_TOP + (int)(g_pickerHue / HUE_BAR_MAX * (SQUARE_SIZE - 1));
	int whiteInset = MARKER_OUTLINE;
	int blackInset = 2 * MARKER_OUTLINE;
	draw_frame(g_uiSurface, hueBarX - blackInset, markerY - blackInset, HUE_BAR_WIDTH + 2 * blackInset,
	           MARKER_HEIGHT + 2 * blackInset, COLOR_BLACK);
	draw_frame(g_uiSurface, hueBarX - whiteInset, markerY - whiteInset, HUE_BAR_WIDTH + 2 * whiteInset,
	           MARKER_HEIGHT + 2 * whiteInset, COLOR_WHITE);
}

static u32 text_color_on(const u8 background[3])
{
	return draw_is_light(background) ? COLOR_BLACK : COLOR_WHITE;
}

// The colour in hand (new) over the one the sheet opened with (old); tap old to go back to it.
static void new_and_old_swatches(int swatchX)
{
	int labelX = swatchX + SWATCH_COLUMN_WIDTH / 2;
	if (g_uiSurface) {
		draw_fill(g_uiSurface, swatchX, PICKER_TOP, SWATCH_COLUMN_WIDTH, NEW_SWATCH_HEIGHT,
		          draw_color(g_color), 255);
		draw_text(g_uiSurface, FONT_SMALL, labelX, NEW_LABEL_Y, text_color_on(g_color), ALIGN_CENTER, "new");
	}
	if (widget_swatch(swatchX, OLD_SWATCH_Y, SWATCH_COLUMN_WIDTH, OLD_SWATCH_HEIGHT, g_colorBefore, false)) {
		color_set(g_colorBefore);
	}
	if (g_uiSurface) {
		draw_text(g_uiSurface, FONT_SMALL, labelX, OLD_SWATCH_Y, text_color_on(g_colorBefore), ALIGN_CENTER,
		          "old");
		draw_text(g_uiSurface, FONT_SMALL, labelX, HEX_CODE_Y, COLOR_DIM, ALIGN_CENTER, "#%02X%02X%02X",
		          g_color[0], g_color[1], g_color[2]);
	}
}

static void palette_swatches(int swatchX)
{
	for (int i = 0; i < PALETTE_SIZE; i++) {
		int x = swatchX + (i % PALETTE_COLUMNS) * PALETTE_STEP;
		int y = PALETTE_TOP + (i / PALETTE_COLUMNS) * PALETTE_STEP;
		bool selected = memcmp(PALETTE[i], g_color, sizeof g_color) == 0;
		if (widget_swatch(x, y, SWATCH_SIZE, SWATCH_SIZE, PALETTE[i], selected)) {
			color_set(PALETTE[i]);
		}
	}
}

static void recent_swatches(int sheetLeft)
{
	int left = sheetLeft + SHEET_LEFT_COLUMN_X;
	widget_label_row(left, RECENT_LABEL_Y, SHEET_CONTENT_WIDTH, "Recent", NULL, true);
	for (int i = 0; i < g_recentColorCount; i++) {
		bool selected = memcmp(g_recentColors[i], g_color, sizeof g_color) == 0;
		if (widget_swatch(left + i * RECENT_STEP, RECENT_TOP, SWATCH_SIZE, SWATCH_SIZE, g_recentColors[i],
		                  selected)) {
			color_set(g_recentColors[i]);
		}
	}
}

void color_sheet(int sheetLeft)
{
	int squareX = sheetLeft + SHEET_LEFT_COLUMN_X;
	int hueBarX = sheetLeft + HUE_BAR_X;
	int swatchX = sheetLeft + SWATCH_COLUMN_X;
	if (!g_uiSurface) {
		picker_input(squareX, hueBarX);
	} else {
		draw_picker(squareX, hueBarX);
	}
	new_and_old_swatches(swatchX);
	palette_swatches(swatchX);
	recent_swatches(sheetLeft);
	if (g_uiSurface) {
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + SHEET_WIDTH / 2, HINT_Y, COLOR_DIM, ALIGN_CENTER,
		          "Tap the colour again or press B to draw");
	}
}
