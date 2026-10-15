// Immediate-mode widgets. Each UI function runs twice a frame: with g_uiSurface == NULL it only takes input,
// with g_uiSurface set it only draws. A press is used by at most one widget; a widget that captures the
// stylus keeps it until it lifts.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "draw.h"

#define DRAG_THRESHOLD 5 // px the stylus moves before a press on a list becomes a drag
#define WIDGET_NO_ICON -1 // the icon of a button that shows only its label
#define WIDGET_OFF_ON_COUNT 2

// The widgets that capture the stylus, one id each.
enum {
	WIDGET_NONE,
	WIDGET_SIZE_SLIDER, // the rail
	WIDGET_ZOOM_BUTTON, // a zoom button on the page, held to repeat
	WIDGET_PALETTE_HUE_RING, // the pop-up palette
	WIDGET_PALETTE_COLOR_SQUARE,
	WIDGET_PALETTE_ZOOM,
	WIDGET_PALETTE_ROTATION,
	WIDGET_COLOR_SQUARE, // the colour sheet
	WIDGET_HUE_BAR,
	WIDGET_BRUSH_LIST, // the brush sheets
	WIDGET_BRUSH_SIZE,
	WIDGET_BRUSH_SMUDGE_STRENGTH,
	WIDGET_BRUSH_OPACITY,
	WIDGET_BRUSH_FLOW,
	WIDGET_BRUSH_HARDNESS,
	WIDGET_BRUSH_SPACING,
	WIDGET_BRUSH_SCATTER,
	WIDGET_BRUSH_PAINT,
	WIDGET_BRUSH_TEXTURE_STRENGTH,
	WIDGET_BRUSH_TEXTURE_SCALE,
	WIDGET_TEXTURE_LIST,
	WIDGET_BRUSH_ROUNDNESS,
	WIDGET_BRUSH_ANGLE,
	WIDGET_BRUSH_SIZE_JITTER,
	WIDGET_BRUSH_FADE_LENGTH,
	WIDGET_BRUSH_MIN_SIZE,
	WIDGET_BRUSH_MIN_OPACITY,
	WIDGET_LAYER_LIST, // the layer sheets
	WIDGET_LAYER_OPACITY,
	WIDGET_LAYER_SOFT_EDGE,
	WIDGET_LAYER_MELT,
	WIDGET_LAYER_OUTLINE_WIDTH,
	WIDGET_LAYER_SHADOW_OPACITY,
	WIDGET_LAYER_SHADOW_X,
	WIDGET_LAYER_SHADOW_Y,
	WIDGET_LAYER_SHADOW_BLUR,
	WIDGET_GROUP_OPACITY,
	WIDGET_DRAWING_LIST // the drawings sheet
};

// The buttons that need a second tap, one id each.
enum {
	ARMED_NONE,
	ARMED_MERGE_DOWN,
	ARMED_DELETE_LAYER,
	ARMED_CONVERT_LAYER,
	ARMED_RECOLOR_LAYER,
	ARMED_MERGE_GROUP,
	ARMED_DELETE_GROUP,
	ARMED_DELETE_BRUSH,
	ARMED_DELETE_DRAWING
};

enum {
	BUTTON_STYLE_NORMAL,
	BUTTON_STYLE_ON,
	BUTTON_STYLE_DIM, // disabled: it takes a tap but does nothing
	BUTTON_STYLE_WARN,
	BUTTON_STYLE_FLAT // no background
};

typedef struct {
	int x;
	int y;
	bool pressed; // touched down this frame
	bool held;
} TouchState;

// A list the stylus drags to scroll.
typedef struct {
	int offset; // px scrolled down
	int startOffset; // the offset when the press began
	int pressX; // where the press began
	int pressY;
	u64 pressTime; // osGetTime() at the press
	bool moved; // the press became a drag
	bool pickedUp; // the press picked up what it is on: it scrolls the list only near its ends
} ScrollList;

extern TouchState g_touch; // the stylus this frame
extern Surface *g_uiSurface; // where to draw, NULL while taking input
extern const char *const WIDGET_OFF_ON_LABELS[WIDGET_OFF_ON_COUNT];

void widget_begin_input(void); // before a frame's input pass: the new press is free
// After the input pass: a lifted stylus releases its widget. True when the frame's press is still free.
bool widget_end_input(void);
bool widget_take_press(void); // a new press not taken yet: takes it
bool widget_press_taken(void);
void widget_capture(int id); // the widget keeps the stylus until it lifts
bool widget_captured(int id);
bool widget_is_dragging(void); // a widget has the stylus
void widget_release(void);

bool widget_touch_in(int x, int y, int w, int h);
bool widget_tapped(int x, int y, int w, int h); // a new press inside, not taken yet: takes it
int widget_enabled_style(bool enabled);
// icon: an IC_ value or WIDGET_NO_ICON. label may be NULL.
bool widget_button(int x, int y, int w, int h, int icon, const char *label, int style);
// Two taps for destructive actions: the first arms the button for a few seconds. id is an ARMED_ value.
bool widget_armed_button(int id, int x, int y, int w, int h, int icon, const char *label, bool enabled);
bool widget_tick_armed(void); // once a frame; true when the armed button has just run out
void widget_disarm(void);
int widget_armed_id(void); // the button waiting for its second tap, ARMED_NONE for none
bool widget_toggle(int x, int y, int w, const char *label, bool on);
void widget_label_row(int x, int y, int w, const char *label, const char *value, bool enabled);
// The label row above a track. Dragging sets *value between minValue and maxValue; true when it changed.
bool widget_slider(int id, int x, int y, int w, int *value, int minValue, int maxValue, bool enabled,
                   const char *label, const char *valueText);
// Returns the tapped segment, otherwise selected.
int widget_segmented(int x, int y, int w, int h, const char *const *labels, int count, int selected,
                     bool enabled);
bool widget_swatch(int x, int y, int w, int h, const u8 color[3], bool selected);
// Input pass only. A press that lifts without a drag clicks (widget_clicked); a picked-up press does not.
void widget_scroll_input(ScrollList *list, int id, int x, int y, int w, int h, int contentHeight);
// Keeps the offset within the content (contentHeight) shown in a list of height h.
void widget_scroll_clamp(ScrollList *list, int h, int contentHeight);
// True while the press on the list has stayed still for holdMs and is not picked up yet.
bool widget_scroll_held_still(const ScrollList *list, int id, int holdMs);
bool widget_clicked(int x, int y, int w, int h); // the list click is inside: takes it
// The scroll bar of a list from top, height px high, when its content is taller.
void widget_scrollbar(int x, int top, int height, int contentHeight, int offset);
// A sheet's back button and title (titleFormat may be NULL). True when back is tapped.
bool widget_sheet_header(int sheetLeft, int font, const char *titleFormat, ...);
