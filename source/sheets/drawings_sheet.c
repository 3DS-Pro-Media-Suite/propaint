#include <stdio.h>
#include <string.h>
#include "drawings_sheet.h"
#include "app_state.h"
#include "document.h"
#include "draw.h"
#include "drawings.h"
#include "export.h"
#include "icons.h"
#include "mascot.h"
#include "paths.h"
#include "session.h"
#include "ui.h"
#include "widgets.h"

// A number as text in a string literal: TO_STRING(CANVAS_WIDTH) is "640".
#define STRINGIFY(value) #value
#define TO_STRING(value) STRINGIFY(value)

// Drawings page layout.
#define LIST_HEIGHT (SHEET_LIST_BOTTOM - SHEET_LIST_TOP)
#define NEW_BUTTON_X 196
#define NEW_BUTTON_WIDTH 74
#define CARD_COLUMNS 3
#define CARDS_X 4
#define CARD_WIDTH 88
#define CARD_ROW_HEIGHT 82
#define CARD_GAP 2
#define CARD_HEIGHT (CARD_ROW_HEIGHT - CARD_GAP)
#define THUMBNAIL_INSET 4 // the thumbnail and the name inside their card
#define THUMBNAIL_FRAME 1 // the open drawing's thumbnail has a frame
#define CARD_NAME_Y (THUMBNAIL_INSET + DRAWING_THUMBNAIL_HEIGHT)
#define EMPTY_HINT_Y 100

// Export page layout.
#define FORMAT_LABEL_Y 34
#define FORMAT_PICKER_Y 52
#define FORMAT_PICKER_HEIGHT 26
#define DESCRIPTION_Y 88
#define DESCRIPTION_LINES 3
#define EXPORT_PATH_Y 146
#define REPLACE_NOTE_Y 162
#define EXPORT_BUTTON_Y 196
#define EXPORT_BUTTON_HEIGHT 32

static const char *const EXPORT_FORMAT_LABELS[EXPORT_FORMAT_COUNT] = {
        [EXPORT_ORA] = "OpenRaster (.ora)",
        [EXPORT_PNG] = "PNG",
};

static const char *const EXPORT_DESCRIPTIONS[EXPORT_FORMAT_COUNT][DESCRIPTION_LINES] = {
        [EXPORT_ORA] = {"Keeps layers, groups and their names,", "opacity, blend modes and clipping.",
                        "Shape layers become pixel layers."},
        [EXPORT_PNG] = {"One flat picture on white paper,",
                        TO_STRING(CANVAS_WIDTH) " x " TO_STRING(CANVAS_HEIGHT) ".", ""},
};

int g_exportFormat = EXPORT_ORA;

static char g_selectedName[DRAWING_NAME_MAX + 1]; // the selected drawing on the Drawings page
static ScrollList g_fileScroll;

void drawings_sheet_open(void)
{
	snprintf(g_selectedName, sizeof g_selectedName, "%s", g_drawingName);
	g_fileScroll.offset = 0;
	ui_open_sheet(SHEET_DRAWINGS);
	session_start_job(SESSION_JOB_LIST, NULL, g_unsaved ? "Saving..." : NULL);
}

// Asks for a name on the system keyboard. False when cancelled or when the text is not a usable name.
static bool ask_name(const char *hint, const char *initialText, char *name, size_t nameSize)
{
	char typed[DRAWING_NAME_MAX + 1];
	if (!ui_ask_text(hint, initialText, typed, DRAWING_NAME_MAX)) {
		return false;
	}
	return drawings_clean_name(typed, name, nameSize);
}

static void draw_card(const DrawingFile *file, int x, int y, bool selected, bool isOpen)
{
	draw_rounded_rect(g_uiSurface, x, y, CARD_WIDTH, CARD_HEIGHT,
	                  selected ? COLOR_ACCENT_DARK : COLOR_WIDGET);
	int thumbX = x + THUMBNAIL_INSET;
	int thumbY = y + THUMBNAIL_INSET;
	if (file->thumbnail) {
		draw_image(g_uiSurface, thumbX, thumbY, file->thumbnail, DRAWING_THUMBNAIL_WIDTH,
		           DRAWING_THUMBNAIL_HEIGHT);
	} else {
		draw_fill(g_uiSurface, thumbX, thumbY, DRAWING_THUMBNAIL_WIDTH, DRAWING_THUMBNAIL_HEIGHT, COLOR_WHITE,
		          255);
		int markX = thumbX + DRAWING_THUMBNAIL_WIDTH / 2;
		int markY = thumbY + (DRAWING_THUMBNAIL_HEIGHT - draw_line_height(FONT_SMALL)) / 2;
		draw_text(g_uiSurface, FONT_SMALL, markX, markY, COLOR_DIM, ALIGN_CENTER, "?");
	}
	if (isOpen) {
		draw_frame(g_uiSurface, thumbX - THUMBNAIL_FRAME, thumbY - THUMBNAIL_FRAME,
		           DRAWING_THUMBNAIL_WIDTH + 2 * THUMBNAIL_FRAME,
		           DRAWING_THUMBNAIL_HEIGHT + 2 * THUMBNAIL_FRAME, COLOR_ACCENT);
	}
	char fittedName[DRAWING_NAME_MAX + 1];
	draw_fit_text(fittedName, sizeof fittedName, file->name, CARD_WIDTH - 2 * THUMBNAIL_INSET);
	draw_text(g_uiSurface, FONT_SMALL, thumbX, y + CARD_NAME_Y, isOpen ? COLOR_ACCENT : COLOR_TEXT,
	          ALIGN_LEFT, "%s", fittedName);
}

// One drawing's card: the first tap selects it, a tap on the selected card opens it.
// Returns true when the page is done for this frame (it closed or a job started).
static bool drawing_card(int index, int x, int y)
{
	DrawingFile *file = &g_drawings[index];
	bool selected = strcmp(file->name, g_selectedName) == 0;
	bool isOpen = strcmp(file->name, g_drawingName) == 0;
	if (g_uiSurface) {
		draw_card(file, x, y, selected, isOpen);
	}
	if (!widget_clicked(x, y, CARD_WIDTH, CARD_HEIGHT)) {
		return false;
	}
	if (!selected) {
		snprintf(g_selectedName, sizeof g_selectedName, "%.*s", (int)sizeof g_selectedName - 1, file->name);
		widget_disarm();
		return false;
	}
	draw_reset_clip();
	if (isOpen) {
		ui_open_sheet(SHEET_NONE);
	} else {
		session_start_job(SESSION_JOB_OPEN, file->name, "Opening...");
	}
	return true;
}

static void copy_selected(bool selectedIsOpen)
{
	if (selectedIsOpen && g_unsaved) {
		session_save(true);
	}
	char copyName[DRAWING_NAME_MAX + 1];
	if (!drawings_duplicate(g_selectedName, copyName, sizeof copyName)) {
		app_toast("Copy failed");
		mascot_event(MASCOT_FAILED);
		return;
	}
	drawings_scan();
	snprintf(g_selectedName, sizeof g_selectedName, "%s", copyName);
	app_toast_name("Copied as ", copyName, "");
}

static void rename_selected(void)
{
	char newName[DRAWING_NAME_MAX + 1];
	if (!ask_name("Drawing name", g_selectedName, newName, sizeof newName)) {
		return;
	}
	if (strcmp(newName, g_selectedName) == 0) {
		return;
	}
	if (drawings_exists(newName)) {
		app_toast_name("", newName, " already exists");
		mascot_event(MASCOT_REFUSED);
		return;
	}
	if (!drawings_rename(g_selectedName, newName)) {
		app_toast("Rename failed");
		mascot_event(MASCOT_FAILED);
		return;
	}
	drawings_scan();
	snprintf(g_selectedName, sizeof g_selectedName, "%s", newName);
	app_toast_name("Renamed to ", newName, "");
}

static void delete_selected(bool selectedIsOpen)
{
	if (!drawings_delete(g_selectedName)) {
		app_toast("Delete failed");
		mascot_event(MASCOT_FAILED);
		return;
	}
	app_toast_name("Deleted ", g_selectedName, "");
	drawings_scan();
	// The open drawing is gone: open the newest other one, or start a new one.
	if (selectedIsOpen) {
		g_unsaved = false;
		g_stateDirty = false;
		if (g_drawingCount) {
			session_start_job(SESSION_JOB_OPEN, g_drawings[0].name, "Opening...");
		} else {
			session_start_job(SESSION_JOB_NEW, NULL, "New drawing...");
		}
	}
	const char *newest = g_drawingCount ? g_drawings[0].name : "";
	snprintf(g_selectedName, sizeof g_selectedName, "%s", newest);
}

// The Open, Copy, Rename and Delete buttons along the bottom act on the selected drawing.
static void drawings_bar(int sheetLeft)
{
	bool hasSelection = drawings_find(g_selectedName) >= 0;
	bool selectedIsOpen = hasSelection && strcmp(g_selectedName, g_drawingName) == 0;
	int barLeft = sheetLeft + SHEET_BAR_X;
	if (widget_button(barLeft, SHEET_BAR_Y, SHEET_BAR_BUTTON_WIDTH, SHEET_BAR_HEIGHT, IC_CHECK,
	                  selectedIsOpen ? "Back" : "Open", widget_enabled_style(hasSelection))) {
		if (selectedIsOpen) {
			ui_open_sheet(SHEET_NONE);
		} else {
			session_start_job(SESSION_JOB_OPEN, g_selectedName, "Opening...");
		}
		return;
	}
	if (widget_button(barLeft + SHEET_BAR_BUTTON_STEP, SHEET_BAR_Y, SHEET_BAR_BUTTON_WIDTH, SHEET_BAR_HEIGHT,
	                  IC_DUPLICATE, "Copy",
	                  widget_enabled_style(hasSelection && g_drawingCount < DRAWINGS_MAX))) {
		copy_selected(selectedIsOpen);
	}
	if (widget_button(barLeft + 2 * SHEET_BAR_BUTTON_STEP, SHEET_BAR_Y, SHEET_BAR_BUTTON_WIDTH,
	                  SHEET_BAR_HEIGHT, WIDGET_NO_ICON, "Rename", widget_enabled_style(hasSelection))) {
		rename_selected();
	}
	if (widget_armed_button(ARMED_DELETE_DRAWING, barLeft + 3 * SHEET_BAR_BUTTON_STEP, SHEET_BAR_Y,
	                        SHEET_BAR_BUTTON_WIDTH, SHEET_BAR_HEIGHT, IC_TRASH, "Delete", hasSelection)) {
		delete_selected(selectedIsOpen);
	}
}

void drawings_sheet_files(int sheetLeft)
{
	if (widget_sheet_header(sheetLeft, FONT_LARGE, "Drawings")) {
		ui_open_sheet(SHEET_MENU);
		return;
	}
	if (widget_button(sheetLeft + NEW_BUTTON_X, SHEET_HEADER_Y, NEW_BUTTON_WIDTH, SHEET_HEADER_HEIGHT, IC_NEW,
	                  "New", BUTTON_STYLE_NORMAL)) {
		session_start_job(SESSION_JOB_NEW, NULL, "Saving...");
		return;
	}
	int rowCount = (g_drawingCount + CARD_COLUMNS - 1) / CARD_COLUMNS;
	int contentHeight = rowCount * CARD_ROW_HEIGHT;
	widget_scroll_input(&g_fileScroll, WIDGET_DRAWING_LIST, sheetLeft, SHEET_LIST_TOP, SHEET_WIDTH,
	                    LIST_HEIGHT, contentHeight);
	draw_set_clip(SHEET_LIST_TOP, SHEET_LIST_BOTTOM);
	for (int i = 0; i < g_drawingCount; i++) {
		int column = i % CARD_COLUMNS;
		int row = i / CARD_COLUMNS;
		int x = sheetLeft + CARDS_X + column * (CARD_WIDTH + CARD_GAP);
		int y = SHEET_LIST_TOP + row * CARD_ROW_HEIGHT - g_fileScroll.offset;
		bool outOfView = y + CARD_ROW_HEIGHT <= SHEET_LIST_TOP || y >= SHEET_LIST_BOTTOM;
		if (outOfView) {
			continue;
		}
		if (drawing_card(i, x, y)) {
			return;
		}
	}
	draw_reset_clip();
	// While the list job is waiting the list is not known yet.
	if (g_uiSurface && !g_drawingCount && session_pending_job() != SESSION_JOB_LIST) {
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + SHEET_WIDTH / 2, EMPTY_HINT_Y, COLOR_DIM, ALIGN_CENTER,
		          "No drawings yet");
	}
	widget_scrollbar(sheetLeft + SHEET_SCROLLBAR_X, SHEET_LIST_TOP, LIST_HEIGHT, contentHeight,
	                 g_fileScroll.offset);
	drawings_bar(sheetLeft);
}

void drawings_sheet_export(int sheetLeft)
{
	if (widget_sheet_header(sheetLeft, FONT_LARGE, "Export")) {
		ui_open_sheet(SHEET_MENU);
		return;
	}
	int left = sheetLeft + SHEET_LEFT_COLUMN_X;
	widget_label_row(left, FORMAT_LABEL_Y, SHEET_CONTENT_WIDTH, "Format", NULL, true);
	g_exportFormat = widget_segmented(left, FORMAT_PICKER_Y, SHEET_CONTENT_WIDTH, FORMAT_PICKER_HEIGHT,
	                                  EXPORT_FORMAT_LABELS, EXPORT_FORMAT_COUNT, g_exportFormat, true);
	if (g_uiSurface) {
		int lineHeight = draw_line_height(FONT_SMALL);
		for (int line = 0; line < DESCRIPTION_LINES; line++) {
			draw_text(g_uiSurface, FONT_SMALL, left, DESCRIPTION_Y + line * lineHeight, COLOR_DIM, ALIGN_LEFT,
			          "%s", EXPORT_DESCRIPTIONS[g_exportFormat][line]);
		}
		// Shown without the leading '/'. A long drawing name is cut so the folder and the extension stay visible.
		const char *folder = draw_format("%s/", EXPORTS_FOLDER + 1);
		const char *extension = draw_format(".%s", export_file_extension(g_exportFormat));
		int nameRoom = SHEET_CONTENT_WIDTH - draw_text_width(FONT_SMALL, folder) -
		               draw_text_width(FONT_SMALL, extension);
		char name[DRAWING_NAME_MAX + 1];
		draw_fit_text(name, sizeof name, g_drawingName, nameRoom);
		draw_text(g_uiSurface, FONT_SMALL, left, EXPORT_PATH_Y, COLOR_TEXT, ALIGN_LEFT, "%s%s%s", folder,
		          name, extension);
		draw_text(g_uiSurface, FONT_SMALL, left, REPLACE_NOTE_Y, COLOR_DISABLED, ALIGN_LEFT,
		          "Exporting again replaces the file.");
	}
	if (widget_button(left, EXPORT_BUTTON_Y, SHEET_CONTENT_WIDTH, EXPORT_BUTTON_HEIGHT, IC_EXPORT, "Export",
	                  BUTTON_STYLE_ON)) {
		session_start_export();
	}
}
