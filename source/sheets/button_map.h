// The hardware button map: the actions, the keys, the defaults for each stylus hand, the Buttons page and the
// action picker of one button.
#pragma once
#include <3ds.h>
#include <stdbool.h>

// Every button does one action the user picks (Buttons page in the menu). Hold actions: "Pan or eraser"
// (held: the stylus pans, tapped: brush / eraser), "Pan" and "Eyedropper" (held: the stylus pans / picks a
// colour).
enum {
	ACTION_NONE,
	ACTION_UNDO,
	ACTION_REDO,
	ACTION_ERASER,
	ACTION_PAN_OR_ERASER,
	ACTION_PAN,
	ACTION_EYEDROPPER,
	ACTION_POPUP,
	ACTION_COLOR,
	ACTION_LAYERS,
	ACTION_LIBRARY,
	ACTION_BRUSH_SETTINGS,
	ACTION_MENU,
	ACTION_FIT,
	ACTION_ZOOM_IN,
	ACTION_ZOOM_OUT,
	ACTION_SIZE_UP,
	ACTION_SIZE_DOWN,
	ACTION_LAYER_UP,
	ACTION_LAYER_DOWN,
	ACTION_RAIL,
	ACTION_SAVE,
	ACTION_COUNT
};

enum {
	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_LEFT,
	BUTTON_RIGHT,
	BUTTON_A,
	BUTTON_B,
	BUTTON_X,
	BUTTON_Y,
	BUTTON_L,
	BUTTON_R,
	BUTTON_ZL,
	BUTTON_ZR,
	BUTTON_START,
	BUTTON_SELECT,
	BUTTON_COUNT
};

extern const u32 g_buttonKeys[BUTTON_COUNT]; // the libctru KEY_* bit of each button
extern u8 g_buttonActions[BUTTON_COUNT]; // the action of each button

void button_map_defaults(void); // the free hand's buttons do the frequent things
bool button_map_action_held(u32 heldKeys, int action);
void button_map_sheet(int sheetLeft);
void button_map_action_sheet(int sheetLeft);
