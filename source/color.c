#include <math.h>
#include <string.h>
#include "color.h"
#include "app_state.h"
#include "color_units.h"
#include "document.h"
#include "math_util.h"
#include "tools.h"

#define HUE_SECTOR_COUNT 6 // HSV splits the hue circle into six sectors
#define HUE_SECTOR_DEGREES 60.0f

// The recent colours of a fresh start: the ink, then red, blue and yellow from the colour sheet's palette.
static const u8 DEFAULT_RECENT_COLORS[][3] = {
        INK_COLOR_DEFAULT, {214, 48, 49}, {50, 110, 220}, {250, 205, 60}};
#define DEFAULT_RECENT_COLOR_COUNT ((int)(sizeof DEFAULT_RECENT_COLORS / sizeof DEFAULT_RECENT_COLORS[0]))

// The picker position of a fresh start: near the ink colour, not converted from it.
#define PICKER_HUE_DEFAULT 235.0f
#define PICKER_SATURATION_DEFAULT 0.5f
#define PICKER_VALUE_DEFAULT 0.2f

u8 g_color[3] = INK_COLOR_DEFAULT;
float g_pickerHue;
float g_pickerSaturation;
float g_pickerValue;
u8 g_recentColors[RECENT_COLORS_MAX][3];
int g_recentColorCount;

void color_init(void)
{
	memcpy(g_recentColors, DEFAULT_RECENT_COLORS, sizeof DEFAULT_RECENT_COLORS);
	g_recentColorCount = DEFAULT_RECENT_COLOR_COUNT;
	g_pickerHue = PICKER_HUE_DEFAULT;
	g_pickerSaturation = PICKER_SATURATION_DEFAULT;
	g_pickerValue = PICKER_VALUE_DEFAULT;
}

void color_hsv_to_rgb(float hue, float saturation, float value, u8 rgb[3])
{
	float chroma = value * saturation;
	float sector = hue / HUE_SECTOR_DEGREES;
	// The middle channel rises and falls across each sector.
	float middle = chroma * (1 - fabsf(fmodf(sector, 2) - 1));
	float r = 0;
	float g = 0;
	float b = 0;
	if (sector < 1) {
		r = chroma;
		g = middle;
	} else if (sector < 2) {
		r = middle;
		g = chroma;
	} else if (sector < 3) {
		g = chroma;
		b = middle;
	} else if (sector < 4) {
		g = middle;
		b = chroma;
	} else if (sector < 5) {
		r = middle;
		b = chroma;
	} else {
		r = chroma;
		b = middle;
	}
	float lowest = value - chroma;
	rgb[0] = (u8)((r + lowest) * 255 + 0.5f);
	rgb[1] = (u8)((g + lowest) * 255 + 0.5f);
	rgb[2] = (u8)((b + lowest) * 255 + 0.5f);
}

void color_rgb_to_hsv(const u8 rgb[3], float *hue, float *saturation, float *value)
{
	float r = rgb[0] / UNIT_TO_BYTE;
	float g = rgb[1] / UNIT_TO_BYTE;
	float b = rgb[2] / UNIT_TO_BYTE;
	float highest = fmaxf(r, fmaxf(g, b));
	float lowest = fminf(r, fminf(g, b));
	float range = highest - lowest;
	*value = highest;
	*saturation = highest > 0 ? range / highest : 0;
	float degrees = 0;
	if (range > 0) {
		// The sector of the highest channel: red starts at 0, green at 2, blue at 4.
		float sector;
		if (highest == r) {
			sector = fmodf((g - b) / range, HUE_SECTOR_COUNT);
		} else if (highest == g) {
			sector = (b - r) / range + 2;
		} else {
			sector = (r - g) / range + 4;
		}
		degrees = sector * HUE_SECTOR_DEGREES;
		if (degrees < 0) {
			degrees += FULL_TURN_DEGREES;
		}
	}
	*hue = degrees;
}

static void back_to_brush_tool(void)
{
	if (g_tool == TOOL_ERASER || g_tool == TOOL_EYEDROPPER) {
		tool_select(TOOL_BRUSH);
	}
}

void color_set_keep_tool(const u8 rgb[3])
{
	memcpy(g_color, rgb, sizeof g_color);
	color_rgb_to_hsv(rgb, &g_pickerHue, &g_pickerSaturation, &g_pickerValue);
}

void color_set(const u8 rgb[3])
{
	color_set_keep_tool(rgb);
	back_to_brush_tool();
}

// Not color_set: going back from RGB would lose the hue of a grey.
void color_set_from_picker(void)
{
	u8 rgb[3];
	color_hsv_to_rgb(g_pickerHue, g_pickerSaturation, g_pickerValue, rgb);
	memcpy(g_color, rgb, sizeof g_color);
	back_to_brush_tool();
}

void color_push_recent(const u8 rgb[3])
{
	int index = 0;
	while (index < g_recentColorCount && memcmp(g_recentColors[index], rgb, sizeof g_recentColors[0]) != 0) {
		index++;
	}
	// A new colour takes a new slot, or the oldest one when the list is full.
	if (index == g_recentColorCount) {
		if (g_recentColorCount < RECENT_COLORS_MAX) {
			g_recentColorCount++;
		} else {
			index = RECENT_COLORS_MAX - 1;
		}
	}
	memmove(g_recentColors[1], g_recentColors[0], (size_t)index * sizeof g_recentColors[0]);
	memcpy(g_recentColors[0], rgb, sizeof g_recentColors[0]);
}
