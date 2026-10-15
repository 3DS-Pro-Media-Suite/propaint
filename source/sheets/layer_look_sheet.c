#include <string.h>
#include "layer_look_sheet.h"
#include "app_state.h"
#include "color.h"
#include "document.h"
#include "draw.h"
#include "icons.h"
#include "layer_convert.h"
#include "layer_stack.h"
#include "mascot.h"
#include "page_view.h"
#include "ui.h"
#include "widgets.h"

// Two columns of controls under the title row.
#define CONTROL_LABEL_HEIGHT 14 // a control sits this far below its label
#define SMALL_BUTTON_HEIGHT 16
#define SEGMENTED_HEIGHT 18
#define GROUP_ICON_X 32
#define GROUP_TITLE_X 52
#define GROUP_RENAME_X 196
#define GROUP_RENAME_WIDTH 74
#define GROUP_TITLE_GAP 4 // between the name and the Rename button
#define GROUP_TITLE_WIDTH (GROUP_RENAME_X - GROUP_TITLE_X - GROUP_TITLE_GAP)
#define GROUP_CONTROLS_Y 30
#define GROUP_CONTROL_ROW_STEP 36
#define GROUP_ACTIONS_Y 112
#define GROUP_ACTION_HEIGHT 26
#define GROUP_ACTION_STEP 32
#define GROUP_NOTE_Y 212
#define LOOK_TITLE_WIDTH 86
#define LOOK_RENAME_X 118
#define LOOK_RENAME_WIDTH 66
#define LOOK_CONVERT_X 188
#define LOOK_CONVERT_WIDTH 82
#define LOOK_CONTROLS_Y 28
#define LOOK_ROW_HEIGHT 30 // one slider or colour row
#define LOOK_NOTE_Y 90
#define LOOK_NOTE_LINE_STEP 18
#define RECOLOR_BUTTON_TOP 6 // from the row's top
#define RECOLOR_BUTTON_HEIGHT 20
#define COLOR_CHIP_HEIGHT 14
#define COLOR_CHIP_FRAME 1
#define SOFTNESS_MAX 16 // half pixels
#define SOFTNESS_TO_PIXELS 0.5f
#define MELT_RADIUS_MAX 8
#define OUTLINE_WIDTH_MAX 12
#define SHADOW_OFFSET_MAX 12
#define SHADOW_BLUR_MAX 8

#define BLEND_NAME_COUNT BLEND_COUNT
#define GROUP_VISIBILITY_COUNT 2
#define GROUP_EXPANDED_COUNT 2

static const char *const BLEND_NAMES[BLEND_NAME_COUNT] = {"Normal", "Multiply", "Add"};
static const char *const GROUP_VISIBILITY_LABELS[GROUP_VISIBILITY_COUNT] = {"Hidden", "Shown"};
static const char *const GROUP_EXPANDED_LABELS[GROUP_EXPANDED_COUNT] = {"Closed", "Open"};

static int g_selectedGroup; // the group the group page shows

void layer_look_sheet_open_group(int groupId)
{
	g_selectedGroup = groupId;
	ui_open_sheet(SHEET_GROUP);
}

// Asks for a layer or group name with the system keyboard. False when cancelled.
static bool ask_layer_name(const char *currentName, char name[LAYER_NAME_SIZE])
{
	return ui_ask_text("Name", currentName, name, LAYER_NAME_SIZE - 1);
}

// The title row of the group page. True when the page closed.
static bool group_header(int sheetLeft, int groupId, Group *group)
{
	if (widget_sheet_header(sheetLeft, FONT_SMALL, NULL)) {
		ui_open_sheet(SHEET_LAYERS);
		return true;
	}
	if (g_uiSurface) {
		char name[LAYER_NAME_SIZE];
		draw_fit_text(name, sizeof name, group->name, GROUP_TITLE_WIDTH);
		draw_icon(g_uiSurface, IC_FOLDER, sheetLeft + GROUP_ICON_X, SHEET_TITLE_Y, COLOR_ACCENT);
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + GROUP_TITLE_X, SHEET_TITLE_Y, COLOR_TEXT, ALIGN_LEFT,
		          "%s", name);
	}
	if (widget_button(sheetLeft + GROUP_RENAME_X, SHEET_HEADER_Y, GROUP_RENAME_WIDTH, SHEET_HEADER_HEIGHT,
	                  WIDGET_NO_ICON, "Rename", BUTTON_STYLE_NORMAL)) {
		char name[LAYER_NAME_SIZE];
		if (ask_layer_name(group->name, name) && group_rename(groupId, name)) {
			g_unsaved = true;
			app_toast("Renamed");
		}
	}
	return false;
}

// Opacity, blend, visibility and whether the list shows the group's layers.
static void group_look_controls(int sheetLeft, Group *group)
{
	int leftX = sheetLeft + SHEET_LEFT_COLUMN_X;
	int rightX = sheetLeft + SHEET_RIGHT_COLUMN_X;
	int y = GROUP_CONTROLS_Y;
	int opacity = group->opacity;
	if (widget_slider(WIDGET_GROUP_OPACITY, leftX, y, SHEET_COLUMN_WIDTH, &opacity, 0, 100, true, "Opacity",
	                  draw_format("%d%%", group->opacity))) {
		group->opacity = opacity;
		page_view_look_changed(false);
	}
	widget_label_row(rightX, y, SHEET_COLUMN_WIDTH, "Blend", NULL, true);
	const char *blendName = BLEND_NAMES[group->blend % BLEND_COUNT];
	if (widget_button(rightX, y + CONTROL_LABEL_HEIGHT, SHEET_COLUMN_WIDTH, SMALL_BUTTON_HEIGHT,
	                  WIDGET_NO_ICON, blendName, BUTTON_STYLE_NORMAL)) {
		group->blend = (group->blend + 1) % BLEND_COUNT;
		page_view_look_changed(false);
	}
	y += GROUP_CONTROL_ROW_STEP;
	int controlY = y + CONTROL_LABEL_HEIGHT;
	widget_label_row(leftX, y, SHEET_COLUMN_WIDTH, "Visible", NULL, true);
	int wasVisible = group->visible ? 1 : 0;
	int visible = widget_segmented(leftX, controlY, SHEET_COLUMN_WIDTH, SEGMENTED_HEIGHT,
	                               GROUP_VISIBILITY_LABELS, GROUP_VISIBILITY_COUNT, wasVisible, true);
	if (visible != wasVisible) {
		group->visible = visible;
		page_view_look_changed(false);
	}
	widget_label_row(rightX, y, SHEET_COLUMN_WIDTH, "In the list", NULL, true);
	int wasExpanded = group->expanded ? 1 : 0;
	int expanded = widget_segmented(rightX, controlY, SHEET_COLUMN_WIDTH, SEGMENTED_HEIGHT,
	                                GROUP_EXPANDED_LABELS, GROUP_EXPANDED_COUNT, wasExpanded, true);
	if (expanded != wasExpanded) {
		group->expanded = expanded;
		g_stateDirty = true;
	}
}

// Ungroup, merge and delete. True when the page closed.
static bool group_actions(int sheetLeft, int groupId)
{
	int x = sheetLeft + SHEET_LEFT_COLUMN_X;
	int y = GROUP_ACTIONS_Y;
	if (widget_button(x, y, SHEET_BOTH_COLUMNS_WIDTH, GROUP_ACTION_HEIGHT, IC_FOLDER,
	                  "Ungroup (keep the layers)", BUTTON_STYLE_NORMAL)) {
		group_delete(groupId, true);
		app_toast("Ungrouped");
		page_view_look_changed(false);
		ui_open_sheet(SHEET_LAYERS);
		return true;
	}
	y += GROUP_ACTION_STEP;
	if (widget_armed_button(ARMED_MERGE_GROUP, x, y, SHEET_BOTH_COLUMNS_WIDTH, GROUP_ACTION_HEIGHT, IC_MERGE,
	                        "Merge into one layer", true)) {
		if (group_merge(groupId)) {
			app_toast("Group merged");
			mascot_event(MASCOT_IDEA);
		} else {
			app_toast("Out of memory");
			mascot_event(MASCOT_FAILED);
		}
		page_view_stack_changed();
		ui_open_sheet(SHEET_LAYERS);
		return true;
	}
	y += GROUP_ACTION_STEP;
	int bottom = 0;
	int top = -1;
	group_layer_range(groupId, &bottom, &top);
	bool layersOutside = top - bottom + 1 < g_document.layerCount; // the document keeps at least one layer
	if (widget_armed_button(ARMED_DELETE_GROUP, x, y, SHEET_BOTH_COLUMNS_WIDTH, GROUP_ACTION_HEIGHT, IC_TRASH,
	                        "Delete the group and its layers", layersOutside)) {
		group_delete(groupId, false);
		app_toast("Group deleted");
		page_view_stack_changed();
		ui_open_sheet(SHEET_LAYERS);
		return true;
	}
	return false;
}

void layer_look_sheet_group(int sheetLeft)
{
	int groupId = g_selectedGroup;
	if (!group_exists(groupId)) {
		ui_open_sheet(SHEET_LAYERS);
		return;
	}
	Group *group = &g_document.groups[groupId];
	if (group_header(sheetLeft, groupId, group)) {
		return;
	}
	group_look_controls(sheetLeft, group);
	if (group_actions(sheetLeft, groupId)) {
		return;
	}
	if (g_uiSurface) {
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + SHEET_WIDTH / 2, GROUP_NOTE_Y, COLOR_DIM, ALIGN_CENTER,
		          "A layer just above a group can clip to it");
	}
}

// A colour row of the layer look: tapping the chip gives it the brush colour.
static bool look_color(int x, int y, const char *label, const u8 color[3], bool enabled)
{
	widget_label_row(x, y, SHEET_COLUMN_WIDTH, label, NULL, enabled);
	if (!g_uiSurface) {
		return enabled && widget_tapped(x, y, SHEET_COLUMN_WIDTH, LOOK_ROW_HEIGHT);
	}
	int chipY = y + CONTROL_LABEL_HEIGHT + COLOR_CHIP_FRAME;
	draw_fill(g_uiSurface, x, chipY, SHEET_COLUMN_WIDTH, COLOR_CHIP_HEIGHT,
	          enabled ? draw_color(color) : COLOR_WIDGET, 255);
	draw_frame(g_uiSurface, x - COLOR_CHIP_FRAME, chipY - COLOR_CHIP_FRAME,
	           SHEET_COLUMN_WIDTH + 2 * COLOR_CHIP_FRAME, COLOR_CHIP_HEIGHT + 2 * COLOR_CHIP_FRAME,
	           COLOR_WIDGET_LIGHT);
	if (enabled) {
		draw_text(g_uiSurface, FONT_SMALL, x + SHEET_COLUMN_WIDTH / 2, y + CONTROL_LABEL_HEIGHT,
		          draw_is_light(color) ? COLOR_BLACK : COLOR_WHITE, ALIGN_CENTER, "tap: brush colour");
	}
	return false;
}

// A slider in one of the two columns. True when the value changed.
static bool look_slider(int id, int x, int y, int *value, int min, int max, bool enabled, const char *label,
                        const char *valueText)
{
	return widget_slider(id, x, y, SHEET_COLUMN_WIDTH, value, min, max, enabled, label, valueText);
}

// The title row of the layer look. True when the page closed or the layer changed type.
static bool look_header(int sheetLeft, Layer *layer, bool isShape)
{
	char name[LAYER_NAME_SIZE];
	draw_fit_text(name, sizeof name, layer->props.name, LOOK_TITLE_WIDTH);
	if (widget_sheet_header(sheetLeft, FONT_SMALL, "%s", name)) {
		ui_open_sheet(SHEET_LAYERS);
		return true;
	}
	if (widget_button(sheetLeft + LOOK_RENAME_X, SHEET_HEADER_Y, LOOK_RENAME_WIDTH, SHEET_HEADER_HEIGHT,
	                  WIDGET_NO_ICON, "Rename", BUTTON_STYLE_NORMAL)) {
		char newName[LAYER_NAME_SIZE];
		if (ask_layer_name(layer->props.name, newName) && layer_rename(g_document.current, newName)) {
			g_unsaved = true;
			app_toast("Renamed");
		}
	}
	const char *convertLabel = isShape ? "To pixels" : "To shape";
	if (!widget_armed_button(ARMED_CONVERT_LAYER, sheetLeft + LOOK_CONVERT_X, SHEET_HEADER_Y,
	                         LOOK_CONVERT_WIDTH, SHEET_HEADER_HEIGHT, WIDGET_NO_ICON, convertLabel, true)) {
		return false;
	}
	bool converted = isShape ? layer_to_pixel(g_document.current) : layer_to_sdf(g_document.current);
	if (converted) {
		app_toast(isShape ? "Now a pixel layer" : "Now a shape layer");
		mascot_event(MASCOT_IDEA);
	} else {
		app_toast("Out of memory");
		mascot_event(MASCOT_FAILED);
	}
	page_view_look_changed(true);
	return true;
}

// The soft edge, melt and outline rows that only shape layers have.
static void edge_controls(Layer *layer, int leftX, int rightX, int y)
{
	if (widget_armed_button(ARMED_RECOLOR_LAYER, leftX, y + RECOLOR_BUTTON_TOP, SHEET_COLUMN_WIDTH,
	                        RECOLOR_BUTTON_HEIGHT, IC_DROP, "Recolour all", true)) {
		layer_recolor(g_document.current, g_color);
		app_toast("Layer recoloured (undo cleared)");
		page_view_look_changed(true);
	}
	int softness = layer->props.softness;
	if (look_slider(WIDGET_LAYER_SOFT_EDGE, rightX, y, &softness, 0, SOFTNESS_MAX, true, "Soft edge",
	                draw_format("%.1f px", layer->props.softness * SOFTNESS_TO_PIXELS))) {
		layer->props.softness = softness;
		page_view_look_changed(true);
	}
	y += LOOK_ROW_HEIGHT;
	int meltRadius = layer->props.meltRadius; // only strokes to come use it: nothing to redraw
	if (look_slider(WIDGET_LAYER_MELT, leftX, y, &meltRadius, 0, MELT_RADIUS_MAX, true, "Melt strokes",
	                draw_format("%d px", layer->props.meltRadius))) {
		layer->props.meltRadius = meltRadius;
		g_unsaved = true;
	}
	int outlineWidth = layer->props.outlineWidth;
	const char *outlineText = outlineWidth ? draw_format("%d px", outlineWidth) : "none";
	if (look_slider(WIDGET_LAYER_OUTLINE_WIDTH, rightX, y, &outlineWidth, 0, OUTLINE_WIDTH_MAX, true,
	                "Outline", outlineText)) {
		layer->props.outlineWidth = outlineWidth;
		page_view_look_changed(true);
	}
	y += LOOK_ROW_HEIGHT;
	if (look_color(leftX, y, "Outline colour", layer->props.outlineColor, layer->props.outlineWidth > 0)) {
		memcpy(layer->props.outlineColor, g_color, sizeof layer->props.outlineColor);
		page_view_look_changed(true);
	}
}

// The drop shadow rows that only shape layers have; the on / off choice sits at the end of row y.
static void shadow_controls(Layer *layer, int leftX, int rightX, int y)
{
	widget_label_row(rightX, y, SHEET_COLUMN_WIDTH, "Drop shadow", NULL, true);
	int shadowChoice =
	        widget_segmented(rightX, y + CONTROL_LABEL_HEIGHT, SHEET_COLUMN_WIDTH, SMALL_BUTTON_HEIGHT,
	                         WIDGET_OFF_ON_LABELS, WIDGET_OFF_ON_COUNT, layer->props.shadowOn, true);
	if (shadowChoice != layer->props.shadowOn) {
		layer->props.shadowOn = shadowChoice;
		page_view_look_changed(true);
	}
	bool shadowOn = layer->props.shadowOn;
	y += LOOK_ROW_HEIGHT;
	if (look_color(leftX, y, "Shadow colour", layer->props.shadowColor, shadowOn)) {
		memcpy(layer->props.shadowColor, g_color, sizeof layer->props.shadowColor);
		page_view_look_changed(true);
	}
	int shadowPercent = layer->props.shadowAlpha * 100 / 255;
	if (look_slider(WIDGET_LAYER_SHADOW_OPACITY, rightX, y, &shadowPercent, 0, 100, shadowOn, "Shadow",
	                draw_format("%d%%", shadowPercent))) {
		layer->props.shadowAlpha = shadowPercent * 255 / 100;
		page_view_look_changed(true);
	}
	y += LOOK_ROW_HEIGHT;
	int shadowX = layer->props.shadowX;
	if (look_slider(WIDGET_LAYER_SHADOW_X, leftX, y, &shadowX, -SHADOW_OFFSET_MAX, SHADOW_OFFSET_MAX,
	                shadowOn, "Shadow x", draw_format("%d px", layer->props.shadowX))) {
		layer->props.shadowX = shadowX;
		page_view_look_changed(true);
	}
	int shadowY = layer->props.shadowY;
	if (look_slider(WIDGET_LAYER_SHADOW_Y, rightX, y, &shadowY, -SHADOW_OFFSET_MAX, SHADOW_OFFSET_MAX,
	                shadowOn, "Shadow y", draw_format("%d px", layer->props.shadowY))) {
		layer->props.shadowY = shadowY;
		page_view_look_changed(true);
	}
	y += LOOK_ROW_HEIGHT;
	int shadowBlur = layer->props.shadowBlur;
	if (look_slider(WIDGET_LAYER_SHADOW_BLUR, leftX, y, &shadowBlur, 0, SHADOW_BLUR_MAX, shadowOn,
	                "Shadow blur", draw_format("%d px", layer->props.shadowBlur))) {
		layer->props.shadowBlur = shadowBlur;
		page_view_look_changed(true);
	}
}

void layer_look_sheet_layer(int sheetLeft)
{
	Layer *layer = document_current_layer();
	bool isShape = layer->props.type == LAYER_SDF;
	if (look_header(sheetLeft, layer, isShape)) {
		return;
	}
	int leftX = sheetLeft + SHEET_LEFT_COLUMN_X;
	int rightX = sheetLeft + SHEET_RIGHT_COLUMN_X;
	int y = LOOK_CONTROLS_Y;
	int opacity = layer->props.opacity;
	if (look_slider(WIDGET_LAYER_OPACITY, leftX, y, &opacity, 0, 100, true, "Opacity",
	                draw_format("%d%%", layer->props.opacity))) {
		layer->props.opacity = opacity;
		page_view_look_changed(false);
	}
	widget_label_row(rightX, y, SHEET_COLUMN_WIDTH, "Blend", NULL, true);
	if (widget_button(rightX, y + CONTROL_LABEL_HEIGHT, SHEET_COLUMN_WIDTH, SMALL_BUTTON_HEIGHT,
	                  WIDGET_NO_ICON, BLEND_NAMES[layer->props.blend], BUTTON_STYLE_NORMAL)) {
		layer->props.blend = (layer->props.blend + 1) % BLEND_COUNT;
		page_view_look_changed(false);
	}
	if (isShape) {
		y += LOOK_ROW_HEIGHT;
		edge_controls(layer, leftX, rightX, y);
		shadow_controls(layer, leftX, rightX, y + 2 * LOOK_ROW_HEIGHT);
		return;
	}
	if (g_uiSurface) {
		int x = sheetLeft + SHEET_WIDTH / 2;
		draw_text(g_uiSurface, FONT_SMALL, x, LOOK_NOTE_Y, COLOR_DIM, ALIGN_CENTER,
		          "Pixel layers keep just what you paint.");
		draw_text(g_uiSurface, FONT_SMALL, x, LOOK_NOTE_Y + LOOK_NOTE_LINE_STEP, COLOR_DIM, ALIGN_CENTER,
		          "Shape layers stay crisp at any zoom");
		draw_text(g_uiSurface, FONT_SMALL, x, LOOK_NOTE_Y + 2 * LOOK_NOTE_LINE_STEP, COLOR_DIM, ALIGN_CENTER,
		          "and get an outline and a drop shadow.");
	}
}
