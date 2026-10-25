#include "ui.h"
#include "app_state.h"
#include "brush_sheets.h"
#include "button_map.h"
#include "color_sheet.h"
#include "draw.h"
#include "drawings_sheet.h"
#include "layer_look_sheet.h"
#include "layers_sheet.h"
#include "menu.h"
#include "page_view.h"
#include "popup.h"
#include "pressure.h"
#include "rail.h"
#include "widgets.h"

#define KEYBOARD_BUTTONS 2 // the system keyboard's Cancel and OK

// The zoom buttons on the page, from the top: +, the zoom level and -.
#define ZOOM_PILL_MARGIN 6 // from the page's top corner beside the rail
#define ZOOM_PILL_WIDTH 34
#define ZOOM_BUTTON_COUNT 3
#define ZOOM_BUTTON_STEP 24
#define ZOOM_PILL_HEIGHT (ZOOM_BUTTON_COUNT * ZOOM_BUTTON_STEP)
#define ZOOM_PILL_INSET 1 // the buttons sit this far inside the pill
#define ZOOM_BUTTON_WIDTH (ZOOM_PILL_WIDTH - 2 * ZOOM_PILL_INSET)
#define ZOOM_BUTTON_HEIGHT (ZOOM_BUTTON_STEP - 2 * ZOOM_PILL_INSET)
#define ZOOM_DIVIDER_INSET 4
#define ZOOM_DIVIDER_HEIGHT 1
#define ZOOM_REPEAT_DELAY 24 // frames a held button waits before it repeats
#define ZOOM_REPEAT_INTERVAL 12 // frames between repeats

// Where each sheet sits in the sheet tree.
typedef struct {
	u8 backTo; // the sheet B goes back to
	u8 railRoot; // the sheet whose rail button opens this one
} SheetLinks;

static const SheetLinks SHEET_LINKS[SHEET_COUNT] = {
        [SHEET_NONE] = {SHEET_NONE, SHEET_NONE},
        [SHEET_COLOR] = {SHEET_NONE, SHEET_COLOR},
        [SHEET_BRUSH_LIBRARY] = {SHEET_NONE, SHEET_BRUSH_LIBRARY},
        [SHEET_LAYERS] = {SHEET_NONE, SHEET_LAYERS},
        [SHEET_LAYER_LOOK] = {SHEET_LAYERS, SHEET_LAYERS},
        [SHEET_MENU] = {SHEET_NONE, SHEET_MENU},
        [SHEET_BUTTONS] = {SHEET_MENU, SHEET_MENU},
        [SHEET_GROUP] = {SHEET_LAYERS, SHEET_LAYERS},
        [SHEET_BRUSH_SETTINGS] = {SHEET_BRUSH_LIBRARY, SHEET_BRUSH_LIBRARY},
        [SHEET_BUTTON_ACTION] = {SHEET_BUTTONS, SHEET_MENU},
        [SHEET_DRAWINGS] = {SHEET_NONE, SHEET_MENU},
        [SHEET_EXPORT] = {SHEET_NONE, SHEET_MENU},
};

static bool g_popupOpen;
static int g_openSheet;

int ui_current_sheet(void)
{
	return g_openSheet;
}

bool ui_popup_is_open(void)
{
	return g_popupOpen;
}

// Leaving one of these sheets writes the settings.
static bool sheet_changes_settings(int sheet)
{
	return sheet == SHEET_COLOR || sheet == SHEET_BRUSH_LIBRARY || sheet == SHEET_BRUSH_SETTINGS;
}

void ui_open_sheet(int sheet)
{
	if (sheet == SHEET_COLOR && g_openSheet != SHEET_COLOR) {
		color_sheet_opened();
	}
	if (sheet_changes_settings(g_openSheet) && sheet != g_openSheet) {
		app_settings_changed();
	}
	if (sheet != SHEET_NONE) {
		g_popupOpen = false;
	}
	g_openSheet = sheet;
	widget_release();
	widget_disarm();
}

int ui_root_sheet(int sheet)
{
	return SHEET_LINKS[sheet].railRoot;
}

void ui_toggle_sheet(int sheet)
{
	bool alreadyOpen = ui_root_sheet(g_openSheet) == sheet;
	ui_open_sheet(alreadyOpen ? SHEET_NONE : sheet);
}

void ui_toggle_popup(void)
{
	bool open = !g_popupOpen;
	ui_open_sheet(SHEET_NONE);
	g_popupOpen = open;
	widget_release();
}

void ui_close_popup(void)
{
	g_popupOpen = false;
}

void ui_go_back(void)
{
	if (g_popupOpen) {
		ui_close_popup();
		return;
	}
	ui_open_sheet(SHEET_LINKS[g_openSheet].backTo);
}

bool ui_ask_text(const char *hint, const char *initialText, char *out, int maxLength)
{
	SwkbdState keyboard;
	swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, KEYBOARD_BUTTONS, maxLength);
	swkbdSetHintText(&keyboard, hint);
	swkbdSetInitialText(&keyboard, initialText);
	swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0); // no character filters
	pressure_pause(); // the keyboard needs the normal touch layout
	if (swkbdInputText(&keyboard, out, maxLength + 1) != SWKBD_BUTTON_CONFIRM) {
		return false;
	}
	app_request_bottom_redraw();
	return true;
}

// The sheet is centred in the drawing area.
static int sheet_left(void)
{
	return page_view_left() + (page_view_right() - page_view_left() - SHEET_WIDTH) / 2;
}

static bool zoom_button(int x, int y, const char *label, int style)
{
	return widget_button(x, y, ZOOM_BUTTON_WIDTH, ZOOM_BUTTON_HEIGHT, WIDGET_NO_ICON, label, style);
}

// Tapping the zoom level fits the page, tapping it again goes back. Holding + or - keeps zooming.
static void run_zoom_buttons(void)
{
	static int heldFrames;
	static int heldDirection; // 1: + is held, -1: -
	int x = g_leftHanded ? page_view_right() - ZOOM_PILL_MARGIN - ZOOM_PILL_WIDTH
	                     : page_view_left() + ZOOM_PILL_MARGIN;
	int y = ZOOM_PILL_MARGIN;
	if (g_uiSurface) {
		draw_rounded_rect(g_uiSurface, x, y, ZOOM_PILL_WIDTH, ZOOM_PILL_HEIGHT, COLOR_RAIL);
	}
	int buttonX = x + ZOOM_PILL_INSET;
	int buttonY = y + ZOOM_PILL_INSET;
	int zoomInStyle = page_view_zoom_step() < ZOOM_STEP_COUNT - 1 ? BUTTON_STYLE_FLAT : BUTTON_STYLE_DIM;
	if (zoom_button(buttonX, buttonY, "+", zoomInStyle)) {
		page_view_step_zoom(1);
		widget_capture(WIDGET_ZOOM_BUTTON);
		heldFrames = 0;
		heldDirection = 1;
	}
	// Below 1 the "x" is left off, as "0.44x" is wider than the button.
	float zoom = page_view_current()->zoom;
	const char *levelFormat = zoom < 1 ? "%.2g" : "%.2gx";
	const char *levelLabel = page_view_zoom_step() != ZOOM_STEP_FIT ? draw_format(levelFormat, zoom) : "fit";
	if (zoom_button(buttonX, buttonY + ZOOM_BUTTON_STEP, levelLabel, BUTTON_STYLE_FLAT)) {
		page_view_toggle_fit();
	}
	int zoomOutStyle = page_view_zoom_step() > ZOOM_STEP_FIT ? BUTTON_STYLE_FLAT : BUTTON_STYLE_DIM;
	if (zoom_button(buttonX, buttonY + 2 * ZOOM_BUTTON_STEP, "-", zoomOutStyle)) {
		page_view_step_zoom(-1);
		widget_capture(WIDGET_ZOOM_BUTTON);
		heldFrames = 0;
		heldDirection = -1;
	}
	bool zoomButtonHeld = !g_uiSurface && widget_captured(WIDGET_ZOOM_BUTTON) && g_touch.held;
	if (zoomButtonHeld) {
		heldFrames++;
		if (heldFrames > ZOOM_REPEAT_DELAY && heldFrames % ZOOM_REPEAT_INTERVAL == 0) {
			page_view_step_zoom(heldDirection);
		}
	}
	if (g_uiSurface) {
		int dividerX = x + ZOOM_DIVIDER_INSET;
		int dividerWidth = ZOOM_PILL_WIDTH - 2 * ZOOM_DIVIDER_INSET;
		draw_fill(g_uiSurface, dividerX, y + ZOOM_BUTTON_STEP, dividerWidth, ZOOM_DIVIDER_HEIGHT,
		          COLOR_WIDGET_LIGHT, 255);
		draw_fill(g_uiSurface, dividerX, y + 2 * ZOOM_BUTTON_STEP, dividerWidth, ZOOM_DIVIDER_HEIGHT,
		          COLOR_WIDGET_LIGHT, 255);
	}
}

static void run_open_sheet(int sheetX)
{
	switch (g_openSheet) {
	case SHEET_COLOR:
		color_sheet(sheetX);
		break;
	case SHEET_BRUSH_LIBRARY:
		brush_sheet_library(sheetX);
		break;
	case SHEET_BRUSH_SETTINGS:
		brush_sheet_settings(sheetX);
		break;
	case SHEET_BUTTON_ACTION:
		button_map_action_sheet(sheetX);
		break;
	case SHEET_LAYERS:
		layers_sheet_list(sheetX);
		break;
	case SHEET_LAYER_LOOK:
		layer_look_sheet_layer(sheetX);
		break;
	case SHEET_GROUP:
		layer_look_sheet_group(sheetX);
		break;
	case SHEET_MENU:
		menu_sheet(sheetX);
		break;
	case SHEET_BUTTONS:
		button_map_sheet(sheetX);
		break;
	case SHEET_DRAWINGS:
		drawings_sheet_files(sheetX);
		break;
	case SHEET_EXPORT:
		drawings_sheet_export(sheetX);
		break;
	}
}

void ui_run(void)
{
	draw_reset_clip();
	if (!g_railHidden) {
		rail_run();
	}
	if (g_popupOpen) {
		// A press the rail took closes the palette.
		if (!g_uiSurface && widget_press_taken()) {
			g_popupOpen = false;
		} else {
			popup_run();
		}
		return;
	}
	if (g_zoomButtonsShown && g_openSheet == SHEET_NONE && !g_penDown && !g_sampling) {
		run_zoom_buttons();
	}
	if (g_openSheet == SHEET_NONE) {
		return;
	}
	int sheetX = sheet_left();
	if (g_uiSurface) {
		draw_fill(g_uiSurface, page_view_left(), 0, page_view_right() - page_view_left(), SCREEN_HEIGHT,
		          COLOR_SHEET, 255);
	}
	run_open_sheet(sheetX);
	if (!g_uiSurface) {
		widget_take_press(); // the sheet covers the page
	}
}
