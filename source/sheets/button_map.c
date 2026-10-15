#include <string.h>
#include "button_map.h"
#include "app_state.h"
#include "draw.h"
#include "icons.h"
#include "mascot.h"
#include "ui.h"
#include "widgets.h"

// Buttons page layout.
#define DEFAULTS_BUTTON_X 190
#define DEFAULTS_BUTTON_WIDTH 80
#define KEY_ROWS 7 // the keys fill two columns
#define KEY_TOP 27
#define KEY_ROW_STEP 27
#define KEY_TILE_HEIGHT 26
#define KEY_TEXT_X 6
#define KEY_NAME_Y (-1)
#define KEY_ACTION_Y 11
#define TOGGLES_Y 217 // the zoom buttons and mascot toggles under the keys
#define SETTING_TOGGLE_HEIGHT 21

// Action picker layout.
#define ACTION_COLUMNS 2
#define ACTION_TOP 26
#define ACTION_ROW_STEP 19
#define ACTION_BUTTON_HEIGHT 18

static const char *const ACTION_LABELS[ACTION_COUNT] = {
        [ACTION_NONE] = "Nothing",
        [ACTION_UNDO] = "Undo",
        [ACTION_REDO] = "Redo",
        [ACTION_ERASER] = "Brush / eraser",
        [ACTION_PAN_OR_ERASER] = "Pan or eraser",
        [ACTION_PAN] = "Pan (hold)",
        [ACTION_EYEDROPPER] = "Eyedropper (hold)",
        [ACTION_POPUP] = "Pop-up palette",
        [ACTION_COLOR] = "Colour",
        [ACTION_LAYERS] = "Layers",
        [ACTION_LIBRARY] = "Brush library",
        [ACTION_BRUSH_SETTINGS] = "Brush settings",
        [ACTION_MENU] = "Menu",
        [ACTION_FIT] = "Fit / zoom back",
        [ACTION_ZOOM_IN] = "Zoom in",
        [ACTION_ZOOM_OUT] = "Zoom out",
        [ACTION_SIZE_UP] = "Size +",
        [ACTION_SIZE_DOWN] = "Size -",
        [ACTION_LAYER_UP] = "Layer above",
        [ACTION_LAYER_DOWN] = "Layer below",
        [ACTION_RAIL] = "Tool rail",
        [ACTION_SAVE] = "Save",
};

const u32 g_buttonKeys[BUTTON_COUNT] = {
        [BUTTON_UP] = KEY_DUP,       [BUTTON_DOWN] = KEY_DDOWN,    [BUTTON_LEFT] = KEY_DLEFT,
        [BUTTON_RIGHT] = KEY_DRIGHT, [BUTTON_A] = KEY_A,           [BUTTON_B] = KEY_B,
        [BUTTON_X] = KEY_X,          [BUTTON_Y] = KEY_Y,           [BUTTON_L] = KEY_L,
        [BUTTON_R] = KEY_R,          [BUTTON_ZL] = KEY_ZL,         [BUTTON_ZR] = KEY_ZR,
        [BUTTON_START] = KEY_START,  [BUTTON_SELECT] = KEY_SELECT,
};

static const char *const BUTTON_LABELS[BUTTON_COUNT] = {
        [BUTTON_UP] = "D-pad up",
        [BUTTON_DOWN] = "D-pad down",
        [BUTTON_LEFT] = "D-pad left",
        [BUTTON_RIGHT] = "D-pad right",
        [BUTTON_A] = "A",
        [BUTTON_B] = "B",
        [BUTTON_X] = "X",
        [BUTTON_Y] = "Y",
        [BUTTON_L] = "L",
        [BUTTON_R] = "R",
        [BUTTON_ZL] = "ZL",
        [BUTTON_ZR] = "ZR",
        [BUTTON_START] = "START",
        [BUTTON_SELECT] = "SELECT",
};

// The pop-up palette sits on the free hand's shoulder button: L and R are the only ones an Old 3DS has.
static const u8 RIGHT_HANDED_DEFAULTS[BUTTON_COUNT] = {
        [BUTTON_UP] = ACTION_SIZE_UP,    [BUTTON_DOWN] = ACTION_SIZE_DOWN,   [BUTTON_LEFT] = ACTION_UNDO,
        [BUTTON_RIGHT] = ACTION_ZOOM_IN, [BUTTON_A] = ACTION_ERASER,         [BUTTON_B] = ACTION_REDO,
        [BUTTON_X] = ACTION_RAIL,        [BUTTON_Y] = ACTION_EYEDROPPER,     [BUTTON_L] = ACTION_POPUP,
        [BUTTON_R] = ACTION_FIT,         [BUTTON_ZL] = ACTION_PAN_OR_ERASER, [BUTTON_ZR] = ACTION_POPUP,
        [BUTTON_START] = ACTION_MENU,    [BUTTON_SELECT] = ACTION_POPUP,
};

static const u8 LEFT_HANDED_DEFAULTS[BUTTON_COUNT] = {
        [BUTTON_UP] = ACTION_SIZE_UP,   [BUTTON_DOWN] = ACTION_SIZE_DOWN, [BUTTON_LEFT] = ACTION_REDO,
        [BUTTON_RIGHT] = ACTION_ERASER, [BUTTON_A] = ACTION_ZOOM_IN,      [BUTTON_B] = ACTION_SIZE_DOWN,
        [BUTTON_X] = ACTION_SIZE_UP,    [BUTTON_Y] = ACTION_UNDO,         [BUTTON_L] = ACTION_FIT,
        [BUTTON_R] = ACTION_POPUP,      [BUTTON_ZL] = ACTION_POPUP,       [BUTTON_ZR] = ACTION_PAN_OR_ERASER,
        [BUTTON_START] = ACTION_MENU,   [BUTTON_SELECT] = ACTION_POPUP,
};

u8 g_buttonActions[BUTTON_COUNT];

static int g_pickedButton; // the button whose action the action picker sets

void button_map_defaults(void)
{
	const u8 *defaults = g_leftHanded ? LEFT_HANDED_DEFAULTS : RIGHT_HANDED_DEFAULTS;
	memcpy(g_buttonActions, defaults, sizeof g_buttonActions);
}

void button_map_sheet(int sheetLeft)
{
	if (widget_sheet_header(sheetLeft, FONT_SMALL, "Buttons")) {
		ui_open_sheet(SHEET_MENU);
		return;
	}
	if (widget_button(sheetLeft + DEFAULTS_BUTTON_X, SHEET_HEADER_Y, DEFAULTS_BUTTON_WIDTH,
	                  SHEET_HEADER_HEIGHT, WIDGET_NO_ICON, "Defaults", BUTTON_STYLE_NORMAL)) {
		button_map_defaults();
		app_settings_changed();
		app_toast(g_leftHanded ? "Left-handed defaults" : "Right-handed defaults");
	}
	int leftTileX = sheetLeft + SHEET_TILE_X;
	int rightTileX = leftTileX + SHEET_TILE_STEP;
	const char *zoomLabel = g_zoomButtonsShown ? "Zoom buttons: on" : "Zoom buttons: off";
	int zoomStyle = g_zoomButtonsShown ? BUTTON_STYLE_ON : BUTTON_STYLE_NORMAL;
	if (widget_button(leftTileX, TOGGLES_Y, SHEET_TILE_WIDTH, SETTING_TOGGLE_HEIGHT, WIDGET_NO_ICON,
	                  zoomLabel, zoomStyle)) {
		g_zoomButtonsShown = !g_zoomButtonsShown;
		app_settings_changed();
	}
	const char *mascotLabel = g_mascotHidden ? "Cort: hidden" : "Cort: shown";
	if (widget_button(rightTileX, TOGGLES_Y, SHEET_TILE_WIDTH, SETTING_TOGGLE_HEIGHT, WIDGET_NO_ICON,
	                  mascotLabel, BUTTON_STYLE_NORMAL)) {
		g_mascotHidden = !g_mascotHidden;
		app_settings_changed();
		if (!g_mascotHidden) {
			mascot_event(MASCOT_HELLO);
		}
	}
	for (int button = 0; button < BUTTON_COUNT; button++) {
		int column = button / KEY_ROWS;
		int row = button % KEY_ROWS;
		int x = leftTileX + column * SHEET_TILE_STEP;
		int y = KEY_TOP + row * KEY_ROW_STEP;
		if (!g_uiSurface) {
			if (widget_tapped(x, y, SHEET_TILE_WIDTH, KEY_TILE_HEIGHT)) {
				g_pickedButton = button;
				ui_open_sheet(SHEET_BUTTON_ACTION);
				return;
			}
			continue;
		}
		int action = g_buttonActions[button];
		u32 actionColor = action != ACTION_NONE ? COLOR_TEXT : COLOR_DISABLED;
		draw_rounded_rect(g_uiSurface, x, y, SHEET_TILE_WIDTH, KEY_TILE_HEIGHT, COLOR_WIDGET);
		draw_text(g_uiSurface, FONT_SMALL, x + KEY_TEXT_X, y + KEY_NAME_Y, COLOR_ACCENT, ALIGN_LEFT, "%s",
		          BUTTON_LABELS[button]);
		draw_text(g_uiSurface, FONT_SMALL, x + KEY_TEXT_X, y + KEY_ACTION_Y, actionColor, ALIGN_LEFT, "%s",
		          ACTION_LABELS[action]);
	}
}

void button_map_action_sheet(int sheetLeft)
{
	int button = g_pickedButton;
	if (widget_sheet_header(sheetLeft, FONT_SMALL, "%s does:", BUTTON_LABELS[button])) {
		ui_open_sheet(SHEET_BUTTONS);
		return;
	}
	for (int action = 0; action < ACTION_COUNT; action++) {
		int column = action % ACTION_COLUMNS;
		int row = action / ACTION_COLUMNS;
		int x = sheetLeft + SHEET_TILE_X + column * SHEET_TILE_STEP;
		int y = ACTION_TOP + row * ACTION_ROW_STEP;
		int style = g_buttonActions[button] == action ? BUTTON_STYLE_ON : BUTTON_STYLE_NORMAL;
		if (widget_button(x, y, SHEET_TILE_WIDTH, ACTION_BUTTON_HEIGHT, WIDGET_NO_ICON, ACTION_LABELS[action],
		                  style)) {
			g_buttonActions[button] = (u8)action;
			app_settings_changed();
			app_toast("%s: %s", BUTTON_LABELS[button], ACTION_LABELS[action]);
			ui_open_sheet(SHEET_BUTTONS);
			return;
		}
	}
}

bool button_map_action_held(u32 heldKeys, int action)
{
	for (int button = 0; button < BUTTON_COUNT; button++) {
		if (g_buttonActions[button] == action && (heldKeys & g_buttonKeys[button])) {
			return true;
		}
	}
	return false;
}
