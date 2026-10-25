#include "menu.h"
#include "app_state.h"
#include "button_map.h"
#include "draw.h"
#include "drawings.h"
#include "drawings_sheet.h"
#include "export.h"
#include "ftp_control.h"
#include "icons.h"
#include "page_view.h"
#include "session.h"
#include "ui.h"
#include "widgets.h"

// The menu is two columns of tiles. A tile has an icon, a label and a line of detail.
#define TILE_TOP 4
#define TILE_ROW_STEP 58
#define TILE_HEIGHT 54
#define TILE_ICON_X 8
#define TILE_ICON_Y 19
#define TILE_TEXT_X 28
#define TILE_LABEL_Y 10
#define TILE_DETAIL_Y 27
#define TILE_TEXT_MARGIN 2
#define TILE_TEXT_WIDTH (SHEET_TILE_WIDTH - TILE_TEXT_X - TILE_TEXT_MARGIN)
#define TILE_NAME_SIZE 24 // the drawing's name, cut to fit a tile

static bool menu_tile(int x, int y, int icon, const char *label, const char *detail)
{
	if (!g_uiSurface) {
		return widget_tapped(x, y, SHEET_TILE_WIDTH, TILE_HEIGHT);
	}
	draw_rounded_rect(g_uiSurface, x, y, SHEET_TILE_WIDTH, TILE_HEIGHT, COLOR_WIDGET);
	draw_icon(g_uiSurface, icon, x + TILE_ICON_X, y + TILE_ICON_Y, COLOR_ACCENT);
	draw_text(g_uiSurface, FONT_SMALL, x + TILE_TEXT_X, y + TILE_LABEL_Y, COLOR_TEXT, ALIGN_LEFT, "%s",
	          label);
	if (detail) {
		draw_text(g_uiSurface, FONT_SMALL, x + TILE_TEXT_X, y + TILE_DETAIL_Y, COLOR_DIM, ALIGN_LEFT, "%s",
		          detail);
	}
	return false;
}

void menu_sheet(int sheetLeft)
{
	int leftX = sheetLeft + SHEET_TILE_X;
	int rightX = sheetLeft + SHEET_TILE_X + SHEET_TILE_STEP;
	int y = TILE_TOP;
	if (menu_tile(leftX, y, IC_SAVE, "Save", g_unsaved ? "not saved" : "all saved")) {
		session_save(false);
	}
	if (menu_tile(rightX, y, IC_EXPORT, "Export",
	              drawings_sheet_export_format() == EXPORT_ORA ? "Layered .ora" : "PNG image")) {
		ui_open_sheet(SHEET_EXPORT);
		return;
	}
	y += TILE_ROW_STEP;
	char name[TILE_NAME_SIZE];
	draw_fit_text(name, sizeof name, g_drawingName, TILE_TEXT_WIDTH);
	if (menu_tile(leftX, y, IC_FOLDER, "Drawings", name)) {
		drawings_sheet_open();
		return;
	}
	if (menu_tile(rightX, y, IC_WIFI, ftp_control_label(), ftp_control_status())) {
		ftp_control_toggle();
	}
	y += TILE_ROW_STEP;
	if (menu_tile(leftX, y, IC_SWAP, "Stylus hand", g_leftHanded ? "left" : "right")) {
		g_leftHanded = !g_leftHanded;
		button_map_defaults();
		page_view_relayout();
		app_settings_changed();
		app_toast(g_leftHanded ? "Left-handed: tools right, buttons reset"
		                       : "Right-handed: tools left, buttons reset");
	}
	if (menu_tile(rightX, y, IC_SIDEBAR, "Tool rail", g_railHidden ? "hidden" : "shown")) {
		g_railHidden = !g_railHidden;
		page_view_relayout();
		app_settings_changed();
	}
	y += TILE_ROW_STEP;
	if (menu_tile(leftX, y, IC_HELP, "Buttons", "map actions")) {
		ui_open_sheet(SHEET_BUTTONS);
	}
	if (menu_tile(rightX, y, IC_POWER, session_quit_failed() ? "Exit anyway" : "Save and exit",
	              "Version " APP_VERSION)) {
		g_quitRequested = true;
	}
}
