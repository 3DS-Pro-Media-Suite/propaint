// The bottom-screen UI: the open sheet or pop-up palette, and one frame's widget pass.
#pragma once
#include <stdbool.h>

// Sheet layout in px: x from the sheet's left edge, y from the top of the screen.
#define SHEET_WIDTH 272
#define SHEET_HEADER_Y 2 // the row of the back button, the title and the header buttons
#define SHEET_HEADER_HEIGHT 22
#define SHEET_BACK_X 2
#define SHEET_BACK_WIDTH 26
#define SHEET_TITLE_X 30
#define SHEET_TITLE_Y 5
#define SHEET_LARGE_TITLE_Y 3
#define SHEET_LIST_TOP 28 // scrolling lists
#define SHEET_LIST_BOTTOM 208
#define SHEET_SCROLLBAR_X 269
#define SHEET_SCROLLBAR_WIDTH 2
#define SHEET_BAR_X 2 // the row of buttons along the bottom
#define SHEET_BAR_Y 213
#define SHEET_BAR_HEIGHT 24
#define SHEET_BAR_BUTTON_WIDTH 66
#define SHEET_BAR_BUTTON_STEP 68
#define SHEET_LEFT_COLUMN_X 8 // two columns of controls
#define SHEET_RIGHT_COLUMN_X 142
#define SHEET_COLUMN_WIDTH 124
#define SHEET_BOTH_COLUMNS_WIDTH (SHEET_RIGHT_COLUMN_X + SHEET_COLUMN_WIDTH - SHEET_LEFT_COLUMN_X)
#define SHEET_CONTENT_WIDTH (SHEET_WIDTH - 2 * SHEET_LEFT_COLUMN_X)
#define SHEET_TILE_X 4 // two columns of tiles
#define SHEET_TILE_WIDTH 130
#define SHEET_TILE_STEP 134

// SHEET_BUTTONS is the button map, SHEET_BUTTON_ACTION the action picker of one button, SHEET_DRAWINGS the
// drawings on the SD card.
enum {
	SHEET_NONE,
	SHEET_COLOR,
	SHEET_BRUSH_LIBRARY,
	SHEET_LAYERS,
	SHEET_LAYER_LOOK,
	SHEET_MENU,
	SHEET_BUTTONS,
	SHEET_GROUP,
	SHEET_BRUSH_SETTINGS,
	SHEET_BUTTON_ACTION,
	SHEET_DRAWINGS,
	SHEET_EXPORT,
	SHEET_COUNT
};

int ui_current_sheet(void); // the open sheet, SHEET_NONE when there is none
bool ui_popup_is_open(void); // the pop-up palette is open
void ui_open_sheet(int sheet);
int ui_root_sheet(int sheet); // the sheet whose rail button opens this one
// Opens the sheet, or closes it when it or a sheet reached from it is open.
void ui_toggle_sheet(int sheet);
void ui_toggle_popup(void);
void ui_close_popup(void);
void ui_go_back(void); // B: closes the pop-up palette, or goes to the sheet the open one came from
// Asks for a text with the system keyboard: at most maxLength characters into out, which holds one more.
// False when cancelled.
bool ui_ask_text(const char *hint, const char *initialText, char *out, int maxLength);
// One frame's pass: the rail, the zoom buttons on the page, then the pop-up palette or the open sheet.
void ui_run(void);
