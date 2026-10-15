#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brush_sheets.h"
#include "app_state.h"
#include "brush_engine.h"
#include "brush_library.h"
#include "brush_preview.h"
#include "color.h"
#include "color_units.h"
#include "document.h"
#include "draw.h"
#include "icons.h"
#include "krita_import.h"
#include "mascot.h"
#include "pressure.h"
#include "rail.h"
#include "texture_tab.h"
#include "tools.h"
#include "ui.h"
#include "widgets.h"

#define PREVIEW_PIXEL_BYTES RGB_PIXEL_BYTES // brush previews are RGB
#define STRIP_PREVIEW_WIDTH 124 // the preview on a library tile
#define STRIP_PREVIEW_HEIGHT 20
#define STRIP_PREVIEW_BYTES (STRIP_PREVIEW_WIDTH * STRIP_PREVIEW_HEIGHT * PREVIEW_PIXEL_BYTES)
#define ICON_PREVIEW_BYTES (BRUSH_ICON_SIZE * BRUSH_ICON_SIZE * PREVIEW_PIXEL_BYTES)

// Brush library sheet layout.
#define LIST_HEIGHT (SHEET_LIST_BOTTOM - SHEET_LIST_TOP)
#define LIBRARY_SETTINGS_BUTTON_X 168
#define LIBRARY_SETTINGS_BUTTON_WIDTH 100
#define LIBRARY_COLUMNS 2
#define LIBRARY_ROW_HEIGHT 42
#define LIBRARY_TILE_HEIGHT 40
#define LIBRARY_PREVIEW_INSET 3
#define LIBRARY_TEXT_Y 23 // the name, the Krita mark and the star, under the preview
#define LIBRARY_NAME_X 5
#define LIBRARY_KRITA_MARK_RIGHT 106
#define LIBRARY_NAME_GAP 2 // between the name with its edited star and the Krita mark
#define LIBRARY_STAR_X 110
#define LIBRARY_STAR_HIT_X 100 // the star's tap area
#define LIBRARY_STAR_HIT_Y 20
#define LIBRARY_STAR_HIT_WIDTH 30
#define LIBRARY_STAR_HIT_HEIGHT 20
#define IMPORT_MESSAGE_MAX 96
#define EDITED_MARK "*" // after the name of the brush in hand when it differs from the library
#define KRITA_MARK "K" // on brushes imported from Krita

// Brush settings sheet layout.
#define SETTINGS_TABS_Y 26
#define WIDE_SEGMENTED_HEIGHT 20 // the tabs and the paint modes
#define SETTINGS_PREVIEW_Y 50
#define SETTINGS_PREVIEW_WIDTH 256
#define SETTINGS_PREVIEW_HEIGHT 30
#define SETTINGS_TOP 86 // the first row of controls, under the preview
#define TIP_ROW_STEP 32
#define MODE_ROW_STEP 24
#define STROKE_ROW_STEP 30
#define PEN_ROW_STEP 34
#define SENSOR_ROW_STEP 22
#define SENSOR_BLOCK_HEIGHT 68 // the three sensor rows and a gap
#define CURVE_ROW_STEP 32
#define LABELED_CONTROL_Y 14 // a control under its label
#define SMOOTHING_CONTROL_Y 13
#define SEGMENTED_HEIGHT 18
#define SMALL_SEGMENTED_HEIGHT 17
#define INLINE_LABEL_Y 2 // a label on the same row as a control
#define SENSOR_TOGGLE_X 66
#define SENSOR_TOGGLE_STEP 64
#define SENSOR_TOGGLE_WIDTH 62

// A remake can take several frames' time on an Old 3DS. While a slider moves the preview waits, and it follows
// once the slider has stood still this long or is let go.
#define SETTINGS_PREVIEW_STEADY_FRAMES 8

enum {
	SETTINGS_TAB_TIP,
	SETTINGS_TAB_STROKE,
	SETTINGS_TAB_TEXTURE,
	SETTINGS_TAB_PRESSURE,
	SETTINGS_TAB_COUNT
};

enum {
	CURVE_LINE,
	CURVE_SOFT,
	CURVE_FIRM,
	CURVE_PRESET_COUNT,
	CURVE_CUSTOM = CURVE_PRESET_COUNT
};

static const char *const SETTINGS_TAB_LABELS[SETTINGS_TAB_COUNT] = {"Tip", "Stroke", "Texture", "Pressure"};
static const char *const SMOOTHING_LABELS[STROKE_SMOOTHING_LEVELS] = {"Off", "Low", "High"};
static const char *const BRUSH_MODE_LABELS[BRUSH_MODE_COUNT] = {"Paint", "Tint", "Smudge", "Smooth"};
static const char *const TIP_LABELS[TIP_COUNT] = {"Round", "Square"};
static const char *const CURVE_LABELS[CURVE_PRESET_COUNT] = {"Line", "Soft", "Firm"};

// Response curves as Brush stores them; all zero means straight.
static const u8 CURVE_PRESETS[CURVE_PRESET_COUNT][BRUSH_CURVE_POINTS] = {
        [CURVE_LINE] = {0, 0, 0, 0, 0},
        [CURVE_SOFT] = {0, 16, 64, 143, 255},
        [CURVE_FIRM] = {0, 128, 180, 221, 255},
};

static const u8 PREVIEW_INK[3] = {30, 32, 46};
static const u8 MIXING_INK[3] = {128, 132, 148}; // smudge and smooth previews

// The library's previews (a strip on each tile, an icon on the pop-up palette), made when first shown.
static u8 g_stripPreviews[BRUSH_LIBRARY_MAX][STRIP_PREVIEW_BYTES];
static u8 g_iconPreviews[BRUSH_LIBRARY_MAX][ICON_PREVIEW_BYTES];
static bool g_stripPreviewReady[BRUSH_LIBRARY_MAX];
static bool g_iconPreviewReady[BRUSH_LIBRARY_MAX];

// Each preview runs a real stroke, so one is made a frame; the others show blank and follow.
static int g_previewBudget; // brush previews that may still be made this frame

// A colour byte drawn over white paper, rounded to the nearest value.
static u8 blend_over_white(int value, int alpha)
{
	return (u8)((value * alpha + 255 * (255 - alpha) + 255 / 2) / 255);
}

// A sample stroke by the real engine (brush_preview_draw: a private scratch layer), over white.
static void make_preview(const Brush *brush, int mode, const u8 color[3], u8 *rgb, int width, int height)
{
	u8 *rgba = malloc((size_t)width * height * RGBA_PIXEL_BYTES);
	if (!rgba) {
		memset(rgb, 255, (size_t)width * height * PREVIEW_PIXEL_BYTES);
		return;
	}
	// The preview always paints: tint, smudge and smooth only change paint already there, so on the empty
	// layer they would show nothing. A smudge brush with paint shows its colour.
	const u8 *strokeColor = color;
	bool mixesOnly = (mode == BRUSH_MODE_SMUDGE && !brush->paint) || mode == BRUSH_MODE_SMOOTH;
	if (mixesOnly) {
		strokeColor = MIXING_INK;
	}
	bool onSdfLayer = document_current_layer()->props.type == LAYER_SDF;
	brush_preview_draw(brush, BRUSH_MODE_PAINT, strokeColor, onSdfLayer, rgba, width, height);
	for (int i = 0; i < width * height; i++) {
		int alpha = rgba[i * RGBA_PIXEL_BYTES + RGBA_ALPHA_OFFSET];
		for (int c = 0; c < PREVIEW_PIXEL_BYTES; c++) {
			rgb[i * PREVIEW_PIXEL_BYTES + c] = blend_over_white(rgba[i * RGBA_PIXEL_BYTES + c], alpha);
		}
	}
	free(rgba);
}

void brush_sheet_reset_preview_budget(void)
{
	g_previewBudget = 1;
}

// A library entry's preview, made now when this frame's budget allows. NULL while it is not made yet.
static const u8 *cached_preview(int index, u8 *pixels, bool *ready, int width, int height)
{
	if (*ready) {
		return pixels;
	}
	if (g_previewBudget <= 0) {
		app_request_bottom_redraw();
		return NULL;
	}
	g_previewBudget--;
	make_preview(&g_library[index].brush, g_library[index].mode, PREVIEW_INK, pixels, width, height);
	*ready = true;
	return pixels;
}

static const u8 *library_strip_preview(int index)
{
	return cached_preview(index, g_stripPreviews[index], &g_stripPreviewReady[index], STRIP_PREVIEW_WIDTH,
	                      STRIP_PREVIEW_HEIGHT);
}

const u8 *brush_sheet_icon(int libraryIndex)
{
	return cached_preview(libraryIndex, g_iconPreviews[libraryIndex], &g_iconPreviewReady[libraryIndex],
	                      BRUSH_ICON_SIZE, BRUSH_ICON_SIZE);
}

// The library entries from first up to end (not included) changed: their previews are made again.
static void forget_previews(int first, int end)
{
	for (int i = first; i < end; i++) {
		g_iconPreviewReady[i] = false;
		g_stripPreviewReady[i] = false;
	}
}

// After library entry deletedIndex is deleted, the previews behind it move down with their entries.
static void shift_previews_after_delete(int deletedIndex)
{
	int movedCount = g_libraryCount - deletedIndex;
	if (movedCount > 0) {
		int next = deletedIndex + 1;
		memmove(g_stripPreviews[deletedIndex], g_stripPreviews[next],
		        (size_t)movedCount * sizeof g_stripPreviews[0]);
		memmove(g_iconPreviews[deletedIndex], g_iconPreviews[next],
		        (size_t)movedCount * sizeof g_iconPreviews[0]);
		memmove(&g_stripPreviewReady[deletedIndex], &g_stripPreviewReady[next],
		        (size_t)movedCount * sizeof g_stripPreviewReady[0]);
		memmove(&g_iconPreviewReady[deletedIndex], &g_iconPreviewReady[next],
		        (size_t)movedCount * sizeof g_iconPreviewReady[0]);
	}
	g_iconPreviewReady[g_libraryCount] = false;
	g_stripPreviewReady[g_libraryCount] = false;
}

// One brush of the library: tap the star to put it on the pop-up palette, tap the rest to pick it.
static void library_tile(int index, int x, int y, bool edited)
{
	LibraryBrush *entry = &g_library[index];
	if (g_uiSurface) {
		bool selected = index == g_presetIndex;
		draw_rounded_rect(g_uiSurface, x, y, SHEET_TILE_WIDTH, LIBRARY_TILE_HEIGHT,
		                  selected ? COLOR_ACCENT_DARK : COLOR_WIDGET);
		int previewX = x + LIBRARY_PREVIEW_INSET;
		int previewY = y + LIBRARY_PREVIEW_INSET;
		const u8 *strip = library_strip_preview(index);
		if (strip) {
			draw_image(g_uiSurface, previewX, previewY, strip, STRIP_PREVIEW_WIDTH, STRIP_PREVIEW_HEIGHT);
		} else {
			draw_fill(g_uiSurface, previewX, previewY, STRIP_PREVIEW_WIDTH, STRIP_PREVIEW_HEIGHT, COLOR_WHITE,
			          255);
		}
		int textY = y + LIBRARY_TEXT_Y;
		// The name always leaves room for the edited star before the Krita mark.
		int markX = LIBRARY_KRITA_MARK_RIGHT - draw_text_width(FONT_SMALL, KRITA_MARK);
		int nameWidth = markX - LIBRARY_NAME_GAP - LIBRARY_NAME_X - draw_text_width(FONT_SMALL, EDITED_MARK);
		char name[BRUSH_NAME_SIZE];
		draw_fit_text(name, sizeof name, entry->brush.name, nameWidth);
		const char *editedMark = selected && edited ? EDITED_MARK : "";
		draw_text(g_uiSurface, FONT_SMALL, x + LIBRARY_NAME_X, textY, COLOR_TEXT, ALIGN_LEFT, "%s%s", name,
		          editedMark);
		if (entry->fromKrita) {
			draw_text(g_uiSurface, FONT_SMALL, x + LIBRARY_KRITA_MARK_RIGHT, textY, COLOR_DISABLED,
			          ALIGN_RIGHT, KRITA_MARK);
		}
		int starIcon = entry->favorite ? IC_STAR : IC_STAR_OUTLINE;
		u32 starColor = entry->favorite ? COLOR_ACCENT : COLOR_DIM;
		draw_icon(g_uiSurface, starIcon, x + LIBRARY_STAR_X, textY, starColor);
	}
	if (widget_clicked(x + LIBRARY_STAR_HIT_X, y + LIBRARY_STAR_HIT_Y, LIBRARY_STAR_HIT_WIDTH,
	                   LIBRARY_STAR_HIT_HEIGHT)) {
		entry->favorite = !entry->favorite;
		brush_library_changed();
		app_toast(entry->favorite ? "On the pop-up palette" : "Off the pop-up palette");
	} else if (widget_clicked(x, y, SHEET_TILE_WIDTH, LIBRARY_TILE_HEIGHT)) {
		brush_library_select(index);
		app_toast("%s", entry->brush.name);
	}
}

// Saves the brush in hand as a new library entry.
static void add_current_brush(void)
{
	LibraryBrush entry;
	memset(&entry, 0, sizeof entry);
	entry.brush = g_brush;
	entry.size = (u8)g_brushRadius;
	entry.mode = (u8)g_brushMode;
	entry.favorite = 1;
	snprintf(entry.brush.name, sizeof entry.brush.name, "Custom %d", g_libraryCount % BRUSH_LIBRARY_MAX + 1);
	int index = brush_library_add(&entry);
	if (index < 0) {
		return;
	}
	g_presetIndex = index;
	forget_previews(index, index + 1);
	brush_library_changed();
	app_toast("Saved as %s", entry.brush.name);
}

// Writes the brush in hand over its library entry and keeps the entry's name.
static void save_over_selected_brush(void)
{
	LibraryBrush *entry = &g_library[g_presetIndex];
	char name[sizeof entry->brush.name];
	memcpy(name, entry->brush.name, sizeof name);
	entry->brush = g_brush;
	memcpy(entry->brush.name, name, sizeof name);
	entry->size = (u8)g_brushRadius;
	entry->mode = (u8)g_brushMode;
	g_brush = entry->brush;
	forget_previews(g_presetIndex, g_presetIndex + 1);
	brush_library_changed();
	app_toast("%s updated", name);
}

static void import_krita_brushes(void)
{
	char message[IMPORT_MESSAGE_MAX];
	int countBefore = g_libraryCount;
	int addedCount = krita_import_folder(message, sizeof message);
	forget_previews(countBefore, g_libraryCount);
	brush_library_changed();
	app_toast("%s", message);
	mascot_event(addedCount ? MASCOT_IDEA : MASCOT_REFUSED);
}

static void delete_selected_brush(void)
{
	int countBefore = g_libraryCount;
	brush_library_delete(g_presetIndex);
	if (g_libraryCount < countBefore) {
		shift_previews_after_delete(g_presetIndex);
		tool_brushes_preset_deleted(g_presetIndex);
	}
	g_presetIndex = -1;
	brush_library_changed();
	app_toast("Brush deleted");
}

// The New, Save, Krita and Delete buttons along the bottom.
static void library_bar(int sheetLeft, bool edited)
{
	int barLeft = sheetLeft + SHEET_BAR_X;
	if (widget_button(barLeft, SHEET_BAR_Y, SHEET_BAR_BUTTON_WIDTH, SHEET_BAR_HEIGHT, IC_ADD_SHAPE_LAYER,
	                  "New", widget_enabled_style(g_libraryCount < BRUSH_LIBRARY_MAX))) {
		add_current_brush();
	}
	if (widget_button(barLeft + SHEET_BAR_BUTTON_STEP, SHEET_BAR_Y, SHEET_BAR_BUTTON_WIDTH, SHEET_BAR_HEIGHT,
	                  IC_SAVE, "Save", widget_enabled_style(g_presetIndex >= 0 && edited))) {
		save_over_selected_brush();
	}
	if (widget_button(barLeft + 2 * SHEET_BAR_BUTTON_STEP, SHEET_BAR_Y, SHEET_BAR_BUTTON_WIDTH,
	                  SHEET_BAR_HEIGHT, IC_IMPORT, "Krita",
	                  widget_enabled_style(g_libraryCount < BRUSH_LIBRARY_MAX))) {
		import_krita_brushes();
	}
	if (widget_armed_button(ARMED_DELETE_BRUSH, barLeft + 3 * SHEET_BAR_BUTTON_STEP, SHEET_BAR_Y,
	                        SHEET_BAR_BUTTON_WIDTH, SHEET_BAR_HEIGHT, IC_TRASH, "Delete",
	                        g_presetIndex >= 0 && g_libraryCount > 1)) {
		delete_selected_brush();
	}
}

void brush_sheet_library(int sheetLeft)
{
	static ScrollList listScroll;
	if (g_uiSurface) {
		// The eraser picks its own brush from the same library.
		const char *title = tool_brush_owner() == TOOL_ERASER ? "Eraser" : "Brushes";
		draw_text(g_uiSurface, FONT_LARGE, sheetLeft + SHEET_LEFT_COLUMN_X, SHEET_LARGE_TITLE_Y, COLOR_TEXT,
		          ALIGN_LEFT, "%s", title);
	}
	if (widget_button(sheetLeft + LIBRARY_SETTINGS_BUTTON_X, SHEET_HEADER_Y, LIBRARY_SETTINGS_BUTTON_WIDTH,
	                  SHEET_HEADER_HEIGHT, IC_SLIDERS, "Settings", BUTTON_STYLE_NORMAL)) {
		ui_open_sheet(SHEET_BRUSH_SETTINGS);
		return;
	}
	int rowCount = (g_libraryCount + LIBRARY_COLUMNS - 1) / LIBRARY_COLUMNS;
	int contentHeight = rowCount * LIBRARY_ROW_HEIGHT;
	widget_scroll_input(&listScroll, WIDGET_BRUSH_LIST, sheetLeft, SHEET_LIST_TOP, SHEET_WIDTH, LIST_HEIGHT,
	                    contentHeight);
	bool edited = brush_library_is_edited();
	draw_set_clip(SHEET_LIST_TOP, SHEET_LIST_BOTTOM);
	for (int i = 0; i < g_libraryCount; i++) {
		int column = i % LIBRARY_COLUMNS;
		int row = i / LIBRARY_COLUMNS;
		int x = sheetLeft + SHEET_TILE_X + column * SHEET_TILE_STEP;
		int y = SHEET_LIST_TOP + row * LIBRARY_ROW_HEIGHT - listScroll.offset;
		bool outOfView = y + LIBRARY_ROW_HEIGHT <= SHEET_LIST_TOP || y >= SHEET_LIST_BOTTOM;
		if (!outOfView) {
			library_tile(i, x, y, edited);
		}
	}
	draw_reset_clip();
	widget_scrollbar(sheetLeft + SHEET_SCROLLBAR_X, SHEET_LIST_TOP, LIST_HEIGHT, contentHeight,
	                 listScroll.offset);
	library_bar(sheetLeft, edited);
}

// A response curve picker: straight, soft (a light touch does less) or firm (a light touch does more). A
// curve that is none of these (a preset's own, or imported from Krita) shows as "own" until another is
// picked.
static void curve_pick(int x, int y, const char *label, u8 curve[BRUSH_CURVE_POINTS])
{
	int current = CURVE_CUSTOM;
	for (int i = 0; i < CURVE_PRESET_COUNT; i++) {
		if (memcmp(curve, CURVE_PRESETS[i], BRUSH_CURVE_POINTS) == 0) {
			current = i;
		}
	}
	const char *customNote = current == CURVE_CUSTOM ? "own" : NULL;
	widget_label_row(x, y, SHEET_COLUMN_WIDTH, label, customNote, true);
	int picked = widget_segmented(x, y + LABELED_CONTROL_Y, SHEET_COLUMN_WIDTH, SMALL_SEGMENTED_HEIGHT,
	                              CURVE_LABELS, CURVE_PRESET_COUNT, current, true);
	if (picked != current && picked < CURVE_PRESET_COUNT) {
		memcpy(curve, CURVE_PRESETS[picked], BRUSH_CURVE_POINTS);
	}
}

// Which of pressure, speed and fade drive one target (size, opacity or flow).
static void sensor_row(int x, int y, const char *label, u8 *sensors)
{
	widget_label_row(x, y + INLINE_LABEL_Y, 0, label, NULL, true);
	int toggleX = x + SENSOR_TOGGLE_X;
	if (widget_toggle(toggleX, y, SENSOR_TOGGLE_WIDTH, "Pressure", *sensors & SENSOR_PRESSURE)) {
		*sensors ^= SENSOR_PRESSURE;
	}
	if (widget_toggle(toggleX + SENSOR_TOGGLE_STEP, y, SENSOR_TOGGLE_WIDTH, "Speed",
	                  *sensors & SENSOR_SPEED)) {
		*sensors ^= SENSOR_SPEED;
	}
	if (widget_toggle(toggleX + 2 * SENSOR_TOGGLE_STEP, y, SENSOR_TOGGLE_WIDTH, "Fade",
	                  *sensors & SENSOR_FADE)) {
		*sensors ^= SENSOR_FADE;
		// A fade length of 0 does nothing, so turning the sensor on picks a usable one.
		if ((*sensors & SENSOR_FADE) && !g_brush.fade) {
			g_brush.fade = BRUSH_FADE_DEFAULT;
		}
	}
}

// A slider for one percentage field of g_brush.
static void brush_field_slider(int id, int x, int y, u8 *field, int min, int max, const char *label)
{
	int value = *field;
	if (widget_slider(id, x, y, SHEET_COLUMN_WIDTH, &value, min, max, true, label,
	                  draw_format("%d%%", *field))) {
		*field = (u8)value;
	}
}

// What the settings preview depends on.
typedef struct {
	Brush brush;
	int mode;
	u8 color[3];
	bool onSdfLayer;
} PreviewInputs;

// The live preview is remade when the stroke would look different, less often while a slider is dragged.
static void settings_preview(int x, bool erasing)
{
	static u8 pixels[SETTINGS_PREVIEW_WIDTH * SETTINGS_PREVIEW_HEIGHT * PREVIEW_PIXEL_BYTES];
	static PreviewInputs shown; // what pixels shows
	static bool ready;
	static PreviewInputs lastWanted; // what the previous frame wanted
	static int steadyFrames; // frames that wanted the same
	if (!g_uiSurface) {
		return;
	}
	// memset and memcmp, because the padding bytes are compared too.
	PreviewInputs wanted;
	memset(&wanted, 0, sizeof wanted);
	wanted.brush = g_brush;
	wanted.mode = erasing ? BRUSH_MODE_PAINT : g_brushMode;
	memcpy(wanted.color, erasing ? PREVIEW_INK : g_color, sizeof wanted.color);
	wanted.onSdfLayer = document_current_layer()->props.type == LAYER_SDF;
	if (memcmp(&wanted, &lastWanted, sizeof wanted) != 0) {
		memcpy(&lastWanted, &wanted, sizeof wanted);
		steadyFrames = 0;
	} else {
		steadyFrames++;
	}
	bool changed = !ready || memcmp(&wanted, &shown, sizeof wanted) != 0;
	if (changed && (!widget_is_dragging() || steadyFrames >= SETTINGS_PREVIEW_STEADY_FRAMES)) {
		make_preview(&wanted.brush, wanted.mode, wanted.color, pixels, SETTINGS_PREVIEW_WIDTH,
		             SETTINGS_PREVIEW_HEIGHT);
		memcpy(&shown, &wanted, sizeof wanted);
		ready = true;
	}
	draw_image(g_uiSurface, x, SETTINGS_PREVIEW_Y, pixels, SETTINGS_PREVIEW_WIDTH, SETTINGS_PREVIEW_HEIGHT);
}

static void settings_tip_tab(int leftColumn, int rightColumn, int y)
{
	int radius = g_brushRadius;
	if (widget_slider(WIDGET_BRUSH_SIZE, leftColumn, y, SHEET_COLUMN_WIDTH, &radius, BRUSH_RADIUS_MIN,
	                  BRUSH_RADIUS_MAX, true, "Size", draw_format("%d px", g_brushRadius))) {
		rail_set_brush_size(radius);
	}
	brush_field_slider(WIDGET_BRUSH_HARDNESS, rightColumn, y, &g_brush.hardness, 0, 100, "Hardness");
	y += TIP_ROW_STEP;
	brush_field_slider(WIDGET_BRUSH_ROUNDNESS, leftColumn, y, &g_brush.ratio, BRUSH_RATIO_MIN, 100,
	                   "Roundness");
	int angle = g_brush.angle;
	if (widget_slider(WIDGET_BRUSH_ANGLE, rightColumn, y, SHEET_COLUMN_WIDTH, &angle, 0, BRUSH_ANGLE_MAX,
	                  true, "Angle", draw_format("%d deg", g_brush.angle))) {
		g_brush.angle = angle;
	}
	y += TIP_ROW_STEP;
	widget_label_row(leftColumn, y, 0, "Tip", NULL, true);
	g_brush.tip = widget_segmented(leftColumn, y + LABELED_CONTROL_Y, SHEET_COLUMN_WIDTH, SEGMENTED_HEIGHT,
	                               TIP_LABELS, TIP_COUNT, g_brush.tip, true);
	widget_label_row(rightColumn, y, 0, "Turn with stroke", NULL, true);
	g_brush.follow =
	        widget_segmented(rightColumn, y + LABELED_CONTROL_Y, SHEET_COLUMN_WIDTH, SEGMENTED_HEIGHT,
	                         WIDGET_OFF_ON_LABELS, WIDGET_OFF_ON_COUNT, g_brush.follow ? 1 : 0, true);
}

static void settings_stroke_tab(int leftColumn, int rightColumn, int y, bool erasing)
{
	if (!erasing) {
		g_brushMode = widget_segmented(leftColumn, y, SHEET_BOTH_COLUMNS_WIDTH, WIDE_SEGMENTED_HEIGHT,
		                               BRUSH_MODE_LABELS, BRUSH_MODE_COUNT, g_brushMode, true);
	} else {
		widget_label_row(leftColumn, y + INLINE_LABEL_Y, 0, "Eraser: paint mode does not apply", NULL, false);
	}
	y += MODE_ROW_STEP;
	brush_field_slider(WIDGET_BRUSH_OPACITY, leftColumn, y, &g_brush.opacity, 1, 100, "Opacity");
	brush_field_slider(WIDGET_BRUSH_FLOW, rightColumn, y, &g_brush.flow, 1, 100, "Flow");
	y += STROKE_ROW_STEP;
	brush_field_slider(WIDGET_BRUSH_SPACING, leftColumn, y, &g_brush.spacing, 1, 100, "Spacing");
	brush_field_slider(WIDGET_BRUSH_SCATTER, rightColumn, y, &g_brush.scatter, 0, 100, "Scatter");
	y += STROKE_ROW_STEP;
	brush_field_slider(WIDGET_BRUSH_SIZE_JITTER, leftColumn, y, &g_brush.jitter, 0, 100, "Size jitter");
	// Wet paint: a smudge brush that also lays down the colour.
	bool smudging = !erasing && g_brushMode == BRUSH_MODE_SMUDGE;
	int paint = g_brush.paint;
	if (widget_slider(WIDGET_BRUSH_PAINT, rightColumn, y, SHEET_COLUMN_WIDTH, &paint, 0, 100, smudging,
	                  "Paint", draw_format("%d%%", g_brush.paint))) {
		g_brush.paint = (u8)paint;
	}
	y += STROKE_ROW_STEP;
	widget_label_row(leftColumn, y, 0, "Smoothing", NULL, true);
	g_strokeSmoothing =
	        widget_segmented(leftColumn, y + SMOOTHING_CONTROL_Y, SHEET_COLUMN_WIDTH, SMALL_SEGMENTED_HEIGHT,
	                         SMOOTHING_LABELS, STROKE_SMOOTHING_LEVELS, g_strokeSmoothing, true);
	bool mixing = !erasing && (g_brushMode == BRUSH_MODE_SMUDGE || g_brushMode == BRUSH_MODE_SMOOTH);
	widget_slider(WIDGET_BRUSH_SMUDGE_STRENGTH, rightColumn, y, SHEET_COLUMN_WIDTH, &g_smudgeStrength, 0, 100,
	              mixing, "Smudge", draw_format("%d%%", g_smudgeStrength));
}

static void settings_pressure_tab(int leftColumn, int rightColumn, int y)
{
	widget_label_row(leftColumn, y, 0, "Pressure pen", NULL, true);
	int penShown = g_pressureWanted ? 1 : 0;
	int penPicked = widget_segmented(leftColumn, y + LABELED_CONTROL_Y, SHEET_COLUMN_WIDTH, SEGMENTED_HEIGHT,
	                                 WIDGET_OFF_ON_LABELS, WIDGET_OFF_ON_COUNT, penShown, true);
	if (penPicked != penShown) {
		pressure_set_wanted(penPicked);
	}
	int fadeLength = g_brush.fade;
	if (widget_slider(WIDGET_BRUSH_FADE_LENGTH, rightColumn, y, SHEET_COLUMN_WIDTH, &fadeLength, 1,
	                  BRUSH_FADE_MAX, true, "Fade", draw_format("%d dabs", g_brush.fade))) {
		g_brush.fade = fadeLength;
	}
	y += PEN_ROW_STEP;
	sensor_row(leftColumn, y, "Size", &g_brush.sizeSensors);
	sensor_row(leftColumn, y + SENSOR_ROW_STEP, "Opacity", &g_brush.opacitySensors);
	sensor_row(leftColumn, y + 2 * SENSOR_ROW_STEP, "Flow", &g_brush.flowSensors);
	y += SENSOR_BLOCK_HEIGHT;
	curve_pick(leftColumn, y, "Size curve", g_brush.sizeCurve);
	curve_pick(rightColumn, y, "Opacity curve", g_brush.opacityCurve);
	y += CURVE_ROW_STEP;
	brush_field_slider(WIDGET_BRUSH_MIN_SIZE, leftColumn, y, &g_brush.sizeMin, 0, 100, "Min size");
	brush_field_slider(WIDGET_BRUSH_MIN_OPACITY, rightColumn, y, &g_brush.opacityMin, 0, 100, "Min opacity");
}

void brush_sheet_settings(int sheetLeft)
{
	static int openTab;
	bool erasing = g_tool == TOOL_ERASER;
	int leftColumn = sheetLeft + SHEET_LEFT_COLUMN_X;
	int rightColumn = sheetLeft + SHEET_RIGHT_COLUMN_X;
	const char *name = g_presetIndex >= 0 ? g_library[g_presetIndex].brush.name : "Brush";
	const char *editedNote = brush_library_is_edited() ? " (edited)" : "";
	if (widget_sheet_header(sheetLeft, FONT_SMALL, "%s%s", name, editedNote)) {
		ui_open_sheet(SHEET_BRUSH_LIBRARY);
		return;
	}
	int pickedTab =
	        widget_segmented(leftColumn, SETTINGS_TABS_Y, SHEET_BOTH_COLUMNS_WIDTH, WIDE_SEGMENTED_HEIGHT,
	                         SETTINGS_TAB_LABELS, SETTINGS_TAB_COUNT, openTab, true);
	if (pickedTab == SETTINGS_TAB_TEXTURE && openTab != SETTINGS_TAB_TEXTURE) {
		texture_tab_opened();
	}
	openTab = pickedTab;
	settings_preview(leftColumn, erasing);
	if (openTab == SETTINGS_TAB_TIP) {
		settings_tip_tab(leftColumn, rightColumn, SETTINGS_TOP);
	} else if (openTab == SETTINGS_TAB_STROKE) {
		settings_stroke_tab(leftColumn, rightColumn, SETTINGS_TOP, erasing);
	} else if (openTab == SETTINGS_TAB_TEXTURE) {
		texture_tab(sheetLeft, SETTINGS_TOP);
	} else {
		settings_pressure_tab(leftColumn, rightColumn, SETTINGS_TOP);
	}
}
