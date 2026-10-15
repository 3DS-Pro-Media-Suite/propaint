// The colour to paint with, the colour picker's position, the recent colours, and HSV / RGB conversion.
#pragma once
#include <3ds.h>

#define RECENT_COLORS_MAX 10

extern u8 g_color[3];
extern float g_pickerHue; // the colour picker's position: hue in degrees (0..360)
extern float g_pickerSaturation; // 0..1
extern float g_pickerValue; // 0..1
extern u8 g_recentColors[RECENT_COLORS_MAX][3]; // newest first
extern int g_recentColorCount;

void color_init(void); // the starting recent colours and picker position
// hue in degrees (0..360), saturation and value 0..1
void color_hsv_to_rgb(float hue, float saturation, float value, u8 rgb[3]);
void color_rgb_to_hsv(const u8 rgb[3], float *hue, float *saturation, float *value);
// Picking a colour means "I want to paint with this": the eraser and the eyedropper hand back to the brush.
void color_set(const u8 rgb[3]);
void color_set_keep_tool(const u8 rgb[3]); // the eyedropper's pick: the tool stays
void color_set_from_picker(void); // the colour at the picker's position, as color_set
void color_push_recent(const u8 rgb[3]); // puts it first in the recent colours

// One pixel of a saturation / value square: white mixed with the pure hue (saturation and value 1) by
// saturation, then darkened by value.
static inline void color_square_rgb(const u8 pureHue[3], float saturation, float value, u8 rgb[3])
{
	for (int c = 0; c < 3; c++) {
		int channel = (int)(value * ((1 - saturation) * 255 + saturation * pureHue[c]));
		rgb[c] = (u8)channel;
	}
}
