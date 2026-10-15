#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "widgets.h"
#include "draw.h"
#include "icons.h"
#include "ui.h"

#define ARMED_FRAMES 180 // a destructive button waits three seconds for its second tap
#define ARMED_LABEL "Sure?"

#define BUTTON_ICON_GAP 4 // between a button's icon and its label
#define TEXT_CENTER_NUDGE 1 // the small font looks centred one pixel lower
#define TOGGLE_HEIGHT 20
#define SEGMENT_GAP 2 // between neighbouring segments
#define SWATCH_RING_WIDTH 1 // a selected swatch has an accent ring with a gap inside it
#define SWATCH_RING_GAP 1
#define SWATCH_FRAME_WIDTH 1 // an unselected swatch has a plain frame

#define SLIDER_HEIGHT 30
#define SLIDER_TOUCH_MARGIN 4 // the track can be grabbed a little past its ends
#define SLIDER_TRACK_Y 19
#define SLIDER_TRACK_HEIGHT 3
#define SLIDER_KNOB_CENTER_Y (SLIDER_TRACK_Y + SLIDER_TRACK_HEIGHT / 2.0f)
#define SLIDER_KNOB_RADIUS 5.5f

#define EDGE_SCROLL_ZONE 14 // a picked-up press this close to a list's ends scrolls the list
#define EDGE_SCROLL_STEP 3 // px per frame

TouchState g_touch;
Surface *g_uiSurface;
const char *const WIDGET_OFF_ON_LABELS[WIDGET_OFF_ON_COUNT] = {"Off", "On"};

static bool g_pressTaken; // a widget has taken this frame's press
static int g_activeWidget; // the widget that has the stylus until it lifts, WIDGET_NONE for none
static int g_armedButton; // the destructive button waiting for its second tap, ARMED_NONE for none
static int g_armedFrames; // frames it keeps waiting
static bool g_listClicked; // a list got a tap without a drag (see widget_scroll_input)
static int g_clickX; // where that tap pressed
static int g_clickY;

void widget_begin_input(void)
{
	g_pressTaken = false;
}

bool widget_end_input(void)
{
	if (!g_touch.held) {
		g_activeWidget = WIDGET_NONE;
	}
	return g_touch.pressed && !g_pressTaken;
}

bool widget_take_press(void)
{
	if (g_uiSurface || !g_touch.pressed || g_pressTaken) {
		return false;
	}
	g_pressTaken = true;
	return true;
}

bool widget_press_taken(void)
{
	return g_pressTaken;
}

void widget_capture(int id)
{
	g_activeWidget = id;
}

bool widget_captured(int id)
{
	return g_activeWidget == id;
}

bool widget_is_dragging(void)
{
	return g_activeWidget != WIDGET_NONE;
}

void widget_release(void)
{
	g_activeWidget = WIDGET_NONE;
}

bool widget_touch_in(int x, int y, int w, int h)
{
	return g_touch.x >= x && g_touch.x < x + w && g_touch.y >= y && g_touch.y < y + h;
}

bool widget_tapped(int x, int y, int w, int h)
{
	if (g_uiSurface || !g_touch.pressed || g_pressTaken || !widget_touch_in(x, y, w, h)) {
		return false;
	}
	g_pressTaken = true;
	return true;
}

int widget_enabled_style(bool enabled)
{
	return enabled ? BUTTON_STYLE_NORMAL : BUTTON_STYLE_DIM;
}

// The y of a small-font line centred in a box.
static int small_text_y(int boxY, int boxHeight)
{
	return boxY + (boxHeight - draw_line_height(FONT_SMALL)) / 2 + TEXT_CENTER_NUDGE;
}

static u32 button_fill_color(int style)
{
	if (style == BUTTON_STYLE_ON) {
		return COLOR_ACCENT;
	}
	if (style == BUTTON_STYLE_WARN) {
		return COLOR_WARNING;
	}
	return COLOR_WIDGET;
}

static u32 button_ink_color(int style)
{
	if (style == BUTTON_STYLE_ON) {
		return COLOR_RAIL;
	}
	if (style == BUTTON_STYLE_DIM) {
		return COLOR_DISABLED;
	}
	return COLOR_TEXT;
}

bool widget_button(int x, int y, int w, int h, int icon, const char *label, int style)
{
	if (!g_uiSurface) {
		bool tapped = widget_tapped(x, y, w, h); // a dim button still takes the press
		return tapped && style != BUTTON_STYLE_DIM;
	}
	u32 fillColor = button_fill_color(style);
	u32 inkColor = button_ink_color(style);
	if (style != BUTTON_STYLE_FLAT) {
		draw_rounded_rect(g_uiSurface, x, y, w, h, fillColor);
	}
	bool hasIcon = icon >= 0;
	int contentWidth = label ? draw_text_width(FONT_SMALL, label) : 0;
	if (hasIcon) {
		contentWidth += ICON_SIZE;
	}
	if (hasIcon && label) {
		contentWidth += BUTTON_ICON_GAP;
	}
	int contentX = x + (w - contentWidth) / 2;
	if (hasIcon) {
		draw_icon(g_uiSurface, icon, contentX, y + (h - ICON_SIZE) / 2, inkColor);
		contentX += ICON_SIZE + BUTTON_ICON_GAP;
	}
	if (label) {
		draw_text(g_uiSurface, FONT_SMALL, contentX, small_text_y(y, h), inkColor, ALIGN_LEFT, "%s", label);
	}
	return false;
}

// The first tap arms the button: it turns red and asks for the second tap. An icon button only turns red.
bool widget_armed_button(int id, int x, int y, int w, int h, int icon, const char *label, bool enabled)
{
	bool armed = g_armedButton == id && g_armedFrames > 0;
	int style = BUTTON_STYLE_NORMAL;
	if (!enabled) {
		style = BUTTON_STYLE_DIM;
	} else if (armed) {
		style = BUTTON_STYLE_WARN;
	}
	const char *shownLabel = armed && label ? ARMED_LABEL : label;
	if (!widget_button(x, y, w, h, icon, shownLabel, style)) {
		return false;
	}
	if (armed) {
		g_armedButton = ARMED_NONE;
		return true;
	}
	g_armedButton = id;
	g_armedFrames = ARMED_FRAMES;
	return false;
}

bool widget_tick_armed(void)
{
	if (g_armedFrames <= 0) {
		return false;
	}
	g_armedFrames--;
	if (g_armedFrames == 0) {
		g_armedButton = ARMED_NONE;
		return true;
	}
	return false;
}

void widget_disarm(void)
{
	g_armedButton = ARMED_NONE;
}

int widget_armed_id(void)
{
	return g_armedFrames > 0 ? g_armedButton : ARMED_NONE;
}

void widget_label_row(int x, int y, int w, const char *label, const char *value, bool enabled)
{
	if (!g_uiSurface) {
		return;
	}
	draw_text(g_uiSurface, FONT_SMALL, x, y, enabled ? COLOR_TEXT : COLOR_DISABLED, ALIGN_LEFT, "%s", label);
	if (value) {
		draw_text(g_uiSurface, FONT_SMALL, x + w, y, enabled ? COLOR_DIM : COLOR_DISABLED, ALIGN_RIGHT, "%s",
		          value);
	}
}

bool widget_slider(int id, int x, int y, int w, int *value, int minValue, int maxValue, bool enabled,
                   const char *label, const char *valueText)
{
	if (!g_uiSurface) {
		int touchX = x - SLIDER_TOUCH_MARGIN;
		int touchWidth = w + 2 * SLIDER_TOUCH_MARGIN;
		if (enabled && widget_tapped(touchX, y, touchWidth, SLIDER_HEIGHT)) {
			g_activeWidget = id;
		}
		if (g_activeWidget != id || !g_touch.held) {
			return false;
		}
		float fraction = (g_touch.x - x) / (float)w;
		int newValue = minValue + (int)floorf(fraction * (maxValue - minValue) + 0.5f);
		if (newValue < minValue) {
			newValue = minValue;
		} else if (newValue > maxValue) {
			newValue = maxValue;
		}
		if (newValue == *value) {
			return false;
		}
		*value = newValue;
		return true;
	}
	widget_label_row(x, y, w, label, valueText, enabled);
	int knobX = x;
	if (maxValue > minValue) {
		knobX += (*value - minValue) * w / (maxValue - minValue);
	}
	int trackY = y + SLIDER_TRACK_Y;
	draw_fill(g_uiSurface, x, trackY, w, SLIDER_TRACK_HEIGHT, COLOR_WIDGET_LIGHT, 255);
	if (enabled) {
		draw_fill(g_uiSurface, x, trackY, knobX - x, SLIDER_TRACK_HEIGHT, COLOR_ACCENT, 255);
	}
	u32 knobColor = COLOR_DISABLED;
	if (enabled) {
		knobColor = g_activeWidget == id ? COLOR_ACCENT : COLOR_TEXT;
	}
	draw_disc(g_uiSurface, knobX, y + SLIDER_KNOB_CENTER_Y, SLIDER_KNOB_RADIUS, knobColor);
	return false;
}

int widget_segmented(int x, int y, int w, int h, const char *const *labels, int count, int selected,
                     bool enabled)
{
	int segmentWidth = w / count;
	for (int i = 0; i < count; i++) {
		int segmentX = x + i * segmentWidth;
		if (!g_uiSurface) {
			if (enabled && widget_tapped(segmentX, y, segmentWidth, h)) {
				selected = i;
			}
			continue;
		}
		bool isSelected = i == selected;
		u32 fillColor = enabled && isSelected ? COLOR_ACCENT : COLOR_WIDGET;
		u32 textColor = COLOR_TEXT;
		if (!enabled) {
			textColor = COLOR_DISABLED;
		} else if (isSelected) {
			textColor = COLOR_RAIL;
		}
		draw_rounded_rect(g_uiSurface, segmentX + SEGMENT_GAP / 2, y, segmentWidth - SEGMENT_GAP, h,
		                  fillColor);
		int textX = segmentX + segmentWidth / 2;
		draw_text(g_uiSurface, FONT_SMALL, textX, small_text_y(y, h), textColor, ALIGN_CENTER, "%s",
		          labels[i]);
	}
	return selected;
}

bool widget_swatch(int x, int y, int w, int h, const u8 color[3], bool selected)
{
	if (!g_uiSurface) {
		return widget_tapped(x, y, w, h);
	}
	if (selected) {
		int ringOutset = SWATCH_RING_WIDTH + SWATCH_RING_GAP;
		draw_fill(g_uiSurface, x - ringOutset, y - ringOutset, w + 2 * ringOutset, h + 2 * ringOutset,
		          COLOR_ACCENT, 255);
		draw_fill(g_uiSurface, x - SWATCH_RING_GAP, y - SWATCH_RING_GAP, w + 2 * SWATCH_RING_GAP,
		          h + 2 * SWATCH_RING_GAP, COLOR_SHEET, 255);
	} else {
		draw_frame(g_uiSurface, x - SWATCH_FRAME_WIDTH, y - SWATCH_FRAME_WIDTH, w + 2 * SWATCH_FRAME_WIDTH,
		           h + 2 * SWATCH_FRAME_WIDTH, COLOR_WIDGET_LIGHT);
	}
	draw_fill(g_uiSurface, x, y, w, h, draw_color(color), 255);
	return false;
}

bool widget_toggle(int x, int y, int w, const char *label, bool on)
{
	int style = on ? BUTTON_STYLE_ON : BUTTON_STYLE_NORMAL;
	return widget_button(x, y, w, TOGGLE_HEIGHT, WIDGET_NO_ICON, label, style);
}

// A picked-up press held near the list's top or bottom scrolls the list that way.
static void scroll_near_ends(ScrollList *list, int y, int h)
{
	if (g_touch.y < y + EDGE_SCROLL_ZONE) {
		list->offset -= EDGE_SCROLL_STEP;
	}
	if (g_touch.y > y + h - EDGE_SCROLL_ZONE) {
		list->offset += EDGE_SCROLL_STEP;
	}
}

static void drag_list(ScrollList *list)
{
	int dragDistance = g_touch.y - list->pressY;
	if (abs(dragDistance) > DRAG_THRESHOLD) {
		list->moved = true;
	}
	if (list->moved) {
		list->offset = list->startOffset - dragDistance;
	}
}

static void take_list_input(ScrollList *list, int id, int x, int y, int w, int h)
{
	g_listClicked = false;
	if (widget_tapped(x, y, w, h)) {
		g_activeWidget = id;
		list->pressX = g_touch.x;
		list->pressY = g_touch.y;
		list->pressTime = osGetTime();
		list->startOffset = list->offset;
		list->moved = false;
		list->pickedUp = false;
		g_clickX = g_touch.x;
		g_clickY = g_touch.y;
	}
	if (g_activeWidget != id) {
		return;
	}
	if (g_touch.held) {
		if (list->pickedUp) {
			scroll_near_ends(list, y, h);
		} else {
			drag_list(list);
		}
	} else if (!list->moved && !list->pickedUp) {
		g_listClicked = true;
	}
}

void widget_scroll_clamp(ScrollList *list, int h, int contentHeight)
{
	int maxOffset = contentHeight - h;
	if (list->offset > maxOffset) {
		list->offset = maxOffset;
	}
	if (list->offset < 0) {
		list->offset = 0;
	}
}

void widget_scroll_input(ScrollList *list, int id, int x, int y, int w, int h, int contentHeight)
{
	// Only the input pass scrolls and clamps: drawing uses the offset that pass left.
	if (g_uiSurface) {
		return;
	}
	take_list_input(list, id, x, y, w, h);
	widget_scroll_clamp(list, h, contentHeight);
}

bool widget_scroll_held_still(const ScrollList *list, int id, int holdMs)
{
	if (g_activeWidget != id || !g_touch.held || list->moved || list->pickedUp) {
		return false;
	}
	return osGetTime() - list->pressTime > (u64)holdMs;
}

bool widget_clicked(int x, int y, int w, int h)
{
	if (g_uiSurface || !g_listClicked) {
		return false;
	}
	bool inside = g_clickX >= x && g_clickX < x + w && g_clickY >= y && g_clickY < y + h;
	if (!inside) {
		return false;
	}
	g_listClicked = false;
	return true;
}

void widget_scrollbar(int x, int top, int height, int contentHeight, int offset)
{
	if (!g_uiSurface || contentHeight <= height) {
		return;
	}
	int barHeight = height * height / contentHeight;
	int barY = top + (height - barHeight) * offset / (contentHeight - height);
	draw_fill(g_uiSurface, x, barY, SHEET_SCROLLBAR_WIDTH, barHeight, COLOR_DISABLED, 255);
}

bool widget_sheet_header(int sheetLeft, int font, const char *titleFormat, ...)
{
	if (widget_button(sheetLeft + SHEET_BACK_X, SHEET_HEADER_Y, SHEET_BACK_WIDTH, SHEET_HEADER_HEIGHT,
	                  IC_BACK, NULL, BUTTON_STYLE_FLAT)) {
		return true;
	}
	if (!g_uiSurface || !titleFormat) {
		return false;
	}
	char title[DRAW_TEXT_SIZE];
	va_list arguments;
	va_start(arguments, titleFormat);
	vsnprintf(title, sizeof title, titleFormat, arguments);
	va_end(arguments);
	int titleY = font == FONT_LARGE ? SHEET_LARGE_TITLE_Y : SHEET_TITLE_Y;
	draw_text(g_uiSurface, font, sheetLeft + SHEET_TITLE_X, titleY, COLOR_TEXT, ALIGN_LEFT, "%s", title);
	return false;
}
