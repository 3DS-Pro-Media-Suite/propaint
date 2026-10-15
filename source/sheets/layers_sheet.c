#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "layers_sheet.h"
#include "app_state.h"
#include "color.h"
#include "document.h"
#include "draw.h"
#include "icons.h"
#include "layer_convert.h"
#include "layer_look_sheet.h"
#include "layer_stack.h"
#include "mascot.h"
#include "page_view.h"
#include "thumbnails.h"
#include "ui.h"
#include "widgets.h"

// The layer list. Rows act when the stylus lifts without having dragged, so a drag that starts on a row
// scrolls instead of selecting it. A hold picks a row up to drag it to another place in the stack.
#define LIST_TOP 2
#define LIST_BOTTOM SHEET_LIST_BOTTOM
#define LIST_HEIGHT (LIST_BOTTOM - LIST_TOP)
#define LIST_ROW_STEP 26
#define LIST_ROW_X 4
#define LIST_ROW_WIDTH 264
#define LIST_ROW_HEIGHT 24
#define LIST_ROW_CONTENT_Y 4 // icons and text, from the row's top
#define LIST_EYE_X 8
#define LIST_EYE_BUTTON_WIDTH 24
#define COLOR_GROUP_ROW RGB(40, 40, 48)
#define GROUP_MARK_INSET 2 // the bar on a group that holds the current layer
#define GROUP_MARK_WIDTH 2
#define GROUP_MARK_HEIGHT (LIST_ROW_HEIGHT - 2 * GROUP_MARK_INSET)
#define GROUP_EXPAND_X 30
#define GROUP_EXPAND_BUTTON_X (LIST_ROW_X + LIST_EYE_BUTTON_WIDTH)
#define GROUP_EXPAND_BUTTON_WIDTH 20
#define GROUP_FOLDER_X 50
#define GROUP_NAME_X 70
#define GROUP_INFO_RIGHT 262
#define GROUP_NAME_GAP 8
#define GROUPED_LAYER_INDENT 12
#define LAYER_CLIP_ICON_X 28
#define LAYER_CLIP_ICON_ROOM 14 // a clipped layer's thumbnail moves right to show the clip icon
#define LAYER_THUMBNAIL_X 30
#define LAYER_NAME_GAP 6
#define LAYER_INFO_RIGHT 238
#define LAYER_LOCK_ICON_X 247
#define LAYER_LOCK_BUTTON_X 242
#define LAYER_LOCK_BUTTON_WIDTH 26
#define PICK_UP_LEFT 30 // a hold between these x picks a row up, away from the eye and the lock button
#define PICK_UP_RIGHT LAYER_LOCK_BUTTON_X
#define HOLD_TO_PICK_UP_MS 400
#define INSERT_LINE_WIDTH 3
#define HAND_ROW_X 40 // the row in hand, under the stylus
#define HAND_ROW_WIDTH 200
#define HINT_GAP 8 // between the last row and the hint under a short list
#define HINT_MAX_ROWS 6 // a longer list leaves no room for the hint

// The row of layer buttons under the list.
#define LAYER_BAR_X 2
#define LAYER_BAR_Y 214
#define LAYER_BAR_HEIGHT 24
#define LAYER_BAR_BUTTON_WIDTH 28
#define LAYER_BAR_BUTTON_STEP 30
#define ARMED_NOTE_Y 192 // "tap again" above the bar
#define ARMED_NOTE_HEIGHT 20
#define ARMED_NOTE_TEXT_Y 194

enum { // the buttons of the layer bar, left to right
	BAR_ADD_PIXEL_LAYER,
	BAR_ADD_SHAPE_LAYER,
	BAR_DUPLICATE,
	BAR_MOVE_UP,
	BAR_MOVE_DOWN,
	BAR_CLIP,
	BAR_GROUP,
	BAR_MERGE_DOWN,
	BAR_DELETE
};

#define GROUP_HEADER_ROW -1 // ListRow.layer of a group's header

typedef struct {
	int group; // 0 when the row is not in a group
	int layer; // stack index, or GROUP_HEADER_ROW
} ListRow;

static ScrollList g_list; // offset: px of the list scrolled above its top
static int g_dragRow; // the row in hand while g_list.pickedUp, an index into the rows
static int g_listScrolledLayer = -1; // the current layer the list last scrolled to

void layers_sheet_reset_scroll(void)
{
	g_listScrolledLayer = -1;
}

// Where the row in hand drops at contentY (px from the top of the whole list): the gap in the stack (see
// layer_drop), the group it goes into (-1: the gap decides, 0: the root) and the y of the insertion line.
static void drop_target(const ListRow *rows, int rowCount, int contentY, int *gap, int *intoGroup, int *lineY)
{
	int rowIndex = contentY < 0 ? -1 : contentY / LIST_ROW_STEP;
	int yInRow = contentY - rowIndex * LIST_ROW_STEP;
	*intoGroup = -1;
	if (rowIndex < 0) {
		*gap = g_document.layerCount;
		*lineY = 0;
		return;
	}
	if (rowIndex >= rowCount) {
		*gap = 0;
		*lineY = rowCount * LIST_ROW_STEP;
		return;
	}
	const ListRow *row = &rows[rowIndex];
	// Onto a layer row: it goes where that layer lives (its group or the root), even at a group's edge.
	if (row->layer >= 0) {
		bool upperHalf = yInRow < LIST_ROW_STEP / 2;
		*gap = upperHalf ? row->layer + 1 : row->layer;
		*lineY = upperHalf ? rowIndex * LIST_ROW_STEP : (rowIndex + 1) * LIST_ROW_STEP;
		*intoGroup = row->group;
		return;
	}
	int bottom = 0;
	int top = -1;
	group_layer_range(row->group, &bottom, &top);
	*gap = top + 1;
	// The top third of a group header drops above the group, the rest into it at its top.
	if (yInRow < LIST_ROW_STEP / 3) {
		*intoGroup = 0;
		*lineY = rowIndex * LIST_ROW_STEP;
	} else {
		*intoGroup = row->group;
		*lineY = (rowIndex + 1) * LIST_ROW_STEP;
	}
}

// The rows from the top of the stack down: a header before the layers of each group, and the group's
// layers while it is expanded. Returns the row count.
static int build_rows(ListRow *rows)
{
	int rowCount = 0;
	int previousGroup = 0;
	for (int i = g_document.layerCount - 1; i >= 0; i--) {
		int group = g_document.layers[i].props.group;
		if (group && group != previousGroup) {
			rows[rowCount] = (ListRow){.group = group, .layer = GROUP_HEADER_ROW};
			rowCount++;
		}
		previousGroup = group;
		if (group && !g_document.groups[group].expanded) {
			continue;
		}
		rows[rowCount] = (ListRow){.group = group, .layer = i};
		rowCount++;
	}
	return rowCount;
}

static void group_row(int sheetLeft, int y, int groupId)
{
	Group *group = &g_document.groups[groupId];
	int bottom = 0;
	int top = -1;
	group_layer_range(groupId, &bottom, &top);
	bool holdsCurrent = g_document.current >= bottom && g_document.current <= top;
	int rowX = sheetLeft + LIST_ROW_X;
	if (g_uiSurface) {
		int contentY = y + LIST_ROW_CONTENT_Y;
		draw_rounded_rect(g_uiSurface, rowX, y, LIST_ROW_WIDTH, LIST_ROW_HEIGHT, COLOR_GROUP_ROW);
		if (holdsCurrent) {
			draw_fill(g_uiSurface, rowX, y + GROUP_MARK_INSET, GROUP_MARK_WIDTH, GROUP_MARK_HEIGHT,
			          COLOR_ACCENT, 255);
		}
		int eyeIcon = group->visible ? IC_EYE : IC_EYE_OFF;
		int expandIcon = group->expanded ? IC_CHEVRON_DOWN : IC_CHEVRON_RIGHT;
		draw_icon(g_uiSurface, eyeIcon, sheetLeft + LIST_EYE_X, contentY, COLOR_TEXT);
		draw_icon(g_uiSurface, expandIcon, sheetLeft + GROUP_EXPAND_X, contentY, COLOR_DIM);
		draw_icon(g_uiSurface, IC_FOLDER, sheetLeft + GROUP_FOLDER_X, contentY, COLOR_ACCENT);
		char name[LAYER_NAME_SIZE];
		const char *opacityText = group->opacity < 100 ? draw_format(" %d%%", group->opacity) : "";
		int layerCount = top - bottom + 1;
		const char *plural = top > bottom ? "s" : "";
		const char *info = draw_format("%d layer%s%s", layerCount, plural, opacityText);
		int nameWidth = GROUP_INFO_RIGHT - GROUP_NAME_X - draw_text_width(FONT_SMALL, info) - GROUP_NAME_GAP;
		draw_fit_text(name, sizeof name, group->name, nameWidth);
		u32 nameColor = group->visible ? COLOR_TEXT : COLOR_DIM;
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + GROUP_NAME_X, contentY, nameColor, ALIGN_LEFT, "%s",
		          name);
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + GROUP_INFO_RIGHT, contentY, COLOR_DIM, ALIGN_RIGHT,
		          "%s", info);
	}
	if (widget_clicked(rowX, y, LIST_EYE_BUTTON_WIDTH, LIST_ROW_HEIGHT)) {
		group->visible = !group->visible;
		page_view_look_changed(false);
	} else if (widget_clicked(sheetLeft + GROUP_EXPAND_BUTTON_X, y, GROUP_EXPAND_BUTTON_WIDTH,
	                          LIST_ROW_HEIGHT)) {
		group->expanded = !group->expanded;
		g_stateDirty = true;
	} else if (widget_clicked(rowX, y, LIST_ROW_WIDTH, LIST_ROW_HEIGHT)) {
		layer_look_sheet_open_group(groupId);
	}
}

static void layer_row(int sheetLeft, int y, int index)
{
	Layer *layer = &g_document.layers[index];
	bool isCurrent = index == g_document.current;
	int indent = layer->props.group ? GROUPED_LAYER_INDENT : 0;
	int clipIconRoom = layer->props.clipped ? LAYER_CLIP_ICON_ROOM : 0;
	int thumbX = sheetLeft + LAYER_THUMBNAIL_X + indent + clipIconRoom;
	int rowX = sheetLeft + LIST_ROW_X + indent;
	int rowWidth = LIST_ROW_WIDTH - indent;
	if (g_uiSurface) {
		int contentY = y + LIST_ROW_CONTENT_Y;
		u32 rowColor = isCurrent ? COLOR_ACCENT_DARK : COLOR_WIDGET;
		int eyeIcon = layer->props.visible ? IC_EYE : IC_EYE_OFF;
		draw_rounded_rect(g_uiSurface, rowX, y, rowWidth, LIST_ROW_HEIGHT, rowColor);
		draw_icon(g_uiSurface, eyeIcon, sheetLeft + LIST_EYE_X + indent, contentY, COLOR_TEXT);
		if (layer->props.clipped) {
			draw_icon(g_uiSurface, IC_CLIP, sheetLeft + LAYER_CLIP_ICON_X + indent, contentY, COLOR_ACCENT);
		}
		thumbnails_draw(g_uiSurface, thumbX, y, index, 255);
		const char *typeText = layer->props.type == LAYER_SDF ? "Shape" : "Pixels";
		const char *opacityText =
		        layer->props.opacity < 100 ? draw_format(" %d%%", layer->props.opacity) : "";
		const char *info = draw_format("%s%s", typeText, opacityText);
		char name[LAYER_NAME_SIZE];
		int nameX = thumbX + LAYER_THUMBNAIL_WIDTH + LAYER_NAME_GAP;
		int infoX = sheetLeft + LAYER_INFO_RIGHT;
		int nameWidth = infoX - draw_text_width(FONT_SMALL, info) - LAYER_NAME_GAP - nameX;
		draw_fit_text(name, sizeof name, layer->props.name, nameWidth);
		u32 nameColor = layer->props.visible ? COLOR_TEXT : COLOR_DIM;
		int lockIcon = layer->props.locked ? IC_LOCK : IC_UNLOCK;
		draw_text(g_uiSurface, FONT_SMALL, nameX, contentY, nameColor, ALIGN_LEFT, "%s", name);
		draw_text(g_uiSurface, FONT_SMALL, infoX, contentY, COLOR_DIM, ALIGN_RIGHT, "%s", info);
		draw_icon(g_uiSurface, lockIcon, sheetLeft + LAYER_LOCK_ICON_X, contentY, COLOR_TEXT);
	}
	if (widget_clicked(rowX, y, LIST_EYE_BUTTON_WIDTH, LIST_ROW_HEIGHT)) {
		layer->props.visible = !layer->props.visible;
		page_view_look_changed(false);
	} else if (widget_clicked(sheetLeft + LAYER_LOCK_BUTTON_X, y, LAYER_LOCK_BUTTON_WIDTH, LIST_ROW_HEIGHT)) {
		layer->props.locked = !layer->props.locked;
		g_unsaved = true;
	} else if (widget_clicked(rowX, y, rowWidth, LIST_ROW_HEIGHT)) {
		if (isCurrent) {
			ui_open_sheet(SHEET_LAYER_LOOK);
		} else if (layer_select(index)) {
			g_stateDirty = true;
		}
	}
}

// The stylus y in px from the top of the whole list.
static int touch_list_y(void)
{
	return g_touch.y - LIST_TOP + g_list.offset;
}

// Scrolls the current layer into view when it changed since the list last scrolled to it.
static void scroll_to_current_layer(const ListRow *rows, int rowCount)
{
	if (g_listScrolledLayer == g_document.current) {
		return;
	}
	for (int k = 0; k < rowCount; k++) {
		if (rows[k].layer != g_document.current) {
			continue;
		}
		int rowTop = k * LIST_ROW_STEP;
		int rowBottom = (k + 1) * LIST_ROW_STEP;
		if (rowTop < g_list.offset) {
			g_list.offset = rowTop;
		}
		if (rowBottom > g_list.offset + LIST_HEIGHT) {
			g_list.offset = rowBottom - LIST_HEIGHT;
		}
	}
	g_listScrolledLayer = g_document.current;
}

// A hold on a row, away from its eye and lock buttons, picks the row up.
static void pick_up_held_row(int sheetLeft, int rowCount)
{
	if (!widget_scroll_held_still(&g_list, WIDGET_LAYER_LIST, HOLD_TO_PICK_UP_MS)) {
		return;
	}
	int pressedRow = (g_list.pressY - LIST_TOP + g_list.startOffset) / LIST_ROW_STEP;
	bool onRow = pressedRow >= 0 && pressedRow < rowCount;
	bool onGrip = g_list.pressX > sheetLeft + PICK_UP_LEFT && g_list.pressX < sheetLeft + PICK_UP_RIGHT;
	if (onRow && onGrip) {
		g_list.pickedUp = true;
		g_dragRow = pressedRow;
		app_toast("Drag to move, lift to drop");
	}
}

static void drop_row_in_hand(const ListRow *rows, int rowCount)
{
	int gap;
	int intoGroup;
	int lineY;
	drop_target(rows, rowCount, touch_list_y(), &gap, &intoGroup, &lineY);
	const ListRow *dragged = &rows[g_dragRow];
	bool moved;
	if (dragged->layer >= 0) {
		moved = layer_drop(dragged->layer, gap, intoGroup);
	} else {
		moved = group_move(dragged->group, gap);
	}
	if (moved) {
		page_view_stack_changed();
		app_toast("Moved");
	} else {
		mascot_event(MASCOT_REFUSED);
	}
	g_list.pickedUp = false;
}

// The rows in view take input or draw themselves.
static void visible_rows(int sheetLeft, const ListRow *rows, int rowCount)
{
	for (int k = 0; k < rowCount; k++) {
		int y = LIST_TOP + k * LIST_ROW_STEP - g_list.offset;
		if (y + LIST_ROW_STEP <= LIST_TOP || y >= LIST_BOTTOM) {
			continue;
		}
		if (rows[k].layer < 0) {
			group_row(sheetLeft, y, rows[k].group);
		} else {
			layer_row(sheetLeft, y, rows[k].layer);
		}
	}
}

// The insertion line (or a frame on the group the row goes into) and the row in hand.
static void draw_row_in_hand(int sheetLeft, const ListRow *rows, int rowCount)
{
	int gap;
	int intoGroup;
	int lineY;
	drop_target(rows, rowCount, touch_list_y(), &gap, &intoGroup, &lineY);
	int screenLineY = LIST_TOP + lineY - g_list.offset;
	int rowX = sheetLeft + LIST_ROW_X;
	if (intoGroup > 0) {
		draw_frame(g_uiSurface, rowX, screenLineY - LIST_ROW_STEP, LIST_ROW_WIDTH, LIST_ROW_HEIGHT,
		           COLOR_ACCENT);
	} else {
		draw_fill(g_uiSurface, rowX, screenLineY - INSERT_LINE_WIDTH / 2, LIST_ROW_WIDTH, INSERT_LINE_WIDTH,
		          COLOR_ACCENT, 255);
	}
	const ListRow *dragged = &rows[g_dragRow];
	int handX = sheetLeft + HAND_ROW_X;
	int handY = g_touch.y - LIST_ROW_HEIGHT / 2;
	draw_rounded_rect(g_uiSurface, handX, handY, HAND_ROW_WIDTH, LIST_ROW_HEIGHT, COLOR_ACCENT_DARK);
	draw_frame(g_uiSurface, handX, handY, HAND_ROW_WIDTH, LIST_ROW_HEIGHT, COLOR_ACCENT);
	const char *name = dragged->layer >= 0 ? g_document.layers[dragged->layer].props.name
	                                       : g_document.groups[dragged->group].name;
	draw_text(g_uiSurface, FONT_SMALL, handX + HAND_ROW_WIDTH / 2, handY + LIST_ROW_CONTENT_Y, COLOR_TEXT,
	          ALIGN_CENTER, "%s", name);
}

// Two lines: on one line the hint would be wider than the sheet.
static void draw_hint(int sheetLeft, int rowCount)
{
	int x = sheetLeft + SHEET_WIDTH / 2;
	int y = LIST_TOP + rowCount * LIST_ROW_STEP + HINT_GAP;
	draw_text(g_uiSurface, FONT_SMALL, x, y, COLOR_DISABLED, ALIGN_CENTER,
	          "Tap the selected layer for its look,");
	draw_text(g_uiSurface, FONT_SMALL, x, y + draw_line_height(FONT_SMALL), COLOR_DISABLED, ALIGN_CENTER,
	          "hold a layer to drag it");
}

static int bar_button_x(int sheetLeft, int slot)
{
	return sheetLeft + LAYER_BAR_X + slot * LAYER_BAR_BUTTON_STEP;
}

static bool bar_button(int sheetLeft, int slot, int icon, int style)
{
	return widget_button(bar_button_x(sheetLeft, slot), LAYER_BAR_Y, LAYER_BAR_BUTTON_WIDTH, LAYER_BAR_HEIGHT,
	                     icon, NULL, style);
}

static bool bar_armed_button(int sheetLeft, int slot, int armedId, int icon, bool enabled)
{
	return widget_armed_button(armedId, bar_button_x(sheetLeft, slot), LAYER_BAR_Y, LAYER_BAR_BUTTON_WIDTH,
	                           LAYER_BAR_HEIGHT, icon, NULL, enabled);
}

// Takes the current layer out of its group, or puts it in a new group.
static void toggle_group(const Layer *layer)
{
	if (layer->props.group) {
		layer_set_group(g_document.current, 0);
		app_toast("Taken out of the group");
	} else if (group_create(g_document.current) > 0) {
		app_toast("Put in a new group");
	} else {
		app_toast("%d groups max", MAX_GROUPS);
	}
	page_view_stack_changed();
}

static void merge_down(void)
{
	int result = layer_merge_down(g_document.current);
	if (result == MERGE_DONE) {
		app_toast("Merged into the layer below");
		mascot_event(MASCOT_IDEA);
	} else if (result == MERGE_NEEDS_NORMAL_BLEND) {
		app_toast("Merging needs Normal blend on both");
		mascot_event(MASCOT_REFUSED);
		return; // refused before anything changed, so the drawing stays saved
	} else if (result == MERGE_BELOW_HIDDEN) {
		app_toast("The layer below is hidden");
		mascot_event(MASCOT_REFUSED);
		return;
	} else {
		// MERGE_NO_MEMORY gets this message too.
		app_toast("Can't merge across groups or clips");
		mascot_event(MASCOT_REFUSED);
	}
	page_view_stack_changed();
}

// What the layer bar's armed button waits for.
static void draw_armed_note(int sheetLeft)
{
	int armedId = widget_armed_id();
	if (!g_uiSurface || armedId == ARMED_NONE) {
		return;
	}
	draw_fill(g_uiSurface, sheetLeft, ARMED_NOTE_Y, SHEET_WIDTH, ARMED_NOTE_HEIGHT, COLOR_SHEET, 255);
	const char *note = armedId == ARMED_MERGE_DOWN ? "Merge down: tap again" : "Delete layer: tap again";
	draw_text(g_uiSurface, FONT_SMALL, sheetLeft + SHEET_WIDTH / 2, ARMED_NOTE_TEXT_Y, COLOR_WARNING,
	          ALIGN_CENTER, "%s", note);
}

// The buttons that add layers or move the current one.
static void stack_buttons(int sheetLeft, const Layer *layer)
{
	int addStyle = widget_enabled_style(g_document.layerCount < MAX_LAYERS);
	if (bar_button(sheetLeft, BAR_ADD_PIXEL_LAYER, IC_ADD_PIXEL_LAYER, addStyle)) {
		int added = layer_add(LAYER_PIXEL, g_color);
		app_toast(added >= 0 ? "New pixel layer" : "Out of memory");
		page_view_stack_changed();
	}
	if (bar_button(sheetLeft, BAR_ADD_SHAPE_LAYER, IC_ADD_SHAPE_LAYER, addStyle)) {
		int added = layer_add(LAYER_SDF, g_color);
		app_toast(added >= 0 ? "New shape layer" : "Out of memory");
		page_view_stack_changed();
	}
	if (bar_button(sheetLeft, BAR_DUPLICATE, IC_DUPLICATE, addStyle)) {
		int copy = layer_duplicate(g_document.current);
		app_toast(copy >= 0 ? "Duplicated" : "Out of memory");
		page_view_stack_changed();
	}
	bool canMoveUp = g_document.current < g_document.layerCount - 1 || layer->props.group;
	if (bar_button(sheetLeft, BAR_MOVE_UP, IC_UP, widget_enabled_style(canMoveUp))) {
		layer_move(g_document.current, 1);
		page_view_stack_changed();
	}
	bool canMoveDown = g_document.current > 0 || layer->props.group;
	if (bar_button(sheetLeft, BAR_MOVE_DOWN, IC_DOWN, widget_enabled_style(canMoveDown))) {
		layer_move(g_document.current, -1);
		page_view_stack_changed();
	}
}

// The row of buttons under the list.
static void layer_bar(int sheetLeft)
{
	Layer *layer = document_current_layer();
	stack_buttons(sheetLeft, layer);
	int clipStyle = BUTTON_STYLE_DIM;
	if (layer->props.clipped) {
		clipStyle = BUTTON_STYLE_ON;
	} else if (g_document.current > 0) {
		clipStyle = BUTTON_STYLE_NORMAL;
	}
	if (bar_button(sheetLeft, BAR_CLIP, IC_CLIP, clipStyle)) {
		bool clip = !layer->props.clipped;
		if (layer_set_clip(g_document.current, clip)) {
			app_toast(clip ? "Clipped to the layer below" : "Not clipped");
		} else {
			app_toast("Nothing below to clip to");
		}
		page_view_look_changed(false);
	}
	int groupStyle = layer->props.group ? BUTTON_STYLE_ON : BUTTON_STYLE_NORMAL;
	if (bar_button(sheetLeft, BAR_GROUP, IC_FOLDER, groupStyle)) {
		toggle_group(layer);
	}
	if (bar_armed_button(sheetLeft, BAR_MERGE_DOWN, ARMED_MERGE_DOWN, IC_MERGE, g_document.current > 0)) {
		merge_down();
	}
	if (bar_armed_button(sheetLeft, BAR_DELETE, ARMED_DELETE_LAYER, IC_TRASH, g_document.layerCount > 1)) {
		layer_delete(g_document.current);
		app_toast("Layer deleted");
		page_view_stack_changed();
	}
	draw_armed_note(sheetLeft);
}

void layers_sheet_list(int sheetLeft)
{
	ListRow rows[MAX_LAYERS + MAX_GROUPS];
	int rowCount = build_rows(rows);
	int contentHeight = rowCount * LIST_ROW_STEP;
	if (!g_uiSurface) {
		scroll_to_current_layer(rows, rowCount);
		// Before the list takes this frame's move: a hold that has just run out picks the row up even when
		// the stylus also crosses the drag threshold now, and the row in hand scrolls the list this frame.
		pick_up_held_row(sheetLeft, rowCount);
	}
	widget_scroll_input(&g_list, WIDGET_LAYER_LIST, sheetLeft, LIST_TOP, SHEET_WIDTH, LIST_HEIGHT,
	                    contentHeight);
	// A layer deleted or folded away this frame shortens the list, so drawing clamps the offset too.
	widget_scroll_clamp(&g_list, LIST_HEIGHT, contentHeight);
	bool inHand = g_list.pickedUp && widget_captured(WIDGET_LAYER_LIST);
	if (!g_uiSurface && inHand && !g_touch.held) {
		drop_row_in_hand(rows, rowCount);
	}
	draw_set_clip(LIST_TOP, LIST_BOTTOM);
	visible_rows(sheetLeft, rows, rowCount);
	if (g_uiSurface && inHand) {
		draw_row_in_hand(sheetLeft, rows, rowCount);
	}
	draw_reset_clip();
	widget_scrollbar(sheetLeft + SHEET_SCROLLBAR_X, LIST_TOP, LIST_HEIGHT, contentHeight, g_list.offset);
	if (g_uiSurface && rowCount <= HINT_MAX_ROWS) {
		draw_hint(sheetLeft, rowCount);
	}
	layer_bar(sheetLeft);
}
