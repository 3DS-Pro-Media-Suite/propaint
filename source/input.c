#include <stdlib.h>
#include "input.h"
#include "app_state.h"
#include "button_map.h"
#include "color.h"
#include "document.h"
#include "history.h"
#include "layer_stack.h"
#include "page_view.h"
#include "pen.h"
#include "pressure.h"
#include "rail.h"
#include "session.h"
#include "tools.h"
#include "touch_samples.h"
#include "ui.h"
#include "widgets.h"

#define PAN_OR_ERASER_TAP_MS 350 // a shorter press that did not pan toggles the eraser

// The size buttons step once, then repeat while held, in bigger steps after a while.
#define SIZE_REPEAT_DELAY_FRAMES 20
#define SIZE_REPEAT_INTERVAL_FRAMES 3
#define SIZE_FAST_DELAY_FRAMES 40
#define SIZE_FAST_STEP 2

#define CIRCLE_PAD_DEAD_ZONE 22
#define CIRCLE_PAD_PAN_SPEED 0.06f // screen px per frame for each unit the pad is pushed

static bool g_panning;
static int g_panLastX; // the stylus position the pan has followed so far
static int g_panLastY;
static u64 g_panOrEraserPressTime;
static bool g_panOrEraserUsedToPan;

bool input_panning(void)
{
	return g_panning;
}

static bool sheet_or_popup_open(void)
{
	return ui_current_sheet() || ui_popup_is_open();
}

static void start_page_touch(int screenX, int screenY, bool panHeld, bool eyedropperHeld)
{
	if (panHeld || g_tool == TOOL_MOVE) {
		g_panning = true;
		g_panLastX = screenX;
		g_panLastY = screenY;
		if (panHeld) {
			g_panOrEraserUsedToPan = true;
		}
		return;
	}
	if (eyedropperHeld && g_tool != TOOL_EYEDROPPER) {
		g_previousTool = g_tool; // the eyedropper returns to this tool
	}
	if (eyedropperHeld || g_tool == TOOL_EYEDROPPER) {
		g_sampling = true;
		pen_pick_color(screenX, screenY);
		return;
	}
	touch_samples_read(NULL, NULL, true); // the stroke skips the samples taken before it
	pen_down(screenX, screenY);
}

static void continue_pan(void)
{
	if (!g_touch.held) {
		g_panning = false;
		return;
	}
	page_view_pan(g_touch.x - g_panLastX, g_touch.y - g_panLastY);
	g_panLastX = g_touch.x;
	g_panLastY = g_touch.y;
}

static void continue_stroke(void)
{
	if (!g_touch.held) {
		pen_up();
		return;
	}
	pressure_read();
	int sampleXs[TOUCH_SAMPLES_MAX];
	int sampleYs[TOUCH_SAMPLES_MAX];
	int sampleCount = touch_samples_read(sampleXs, sampleYs, false);
	for (int i = 0; i < sampleCount; i++) {
		pen_move(sampleXs[i], sampleYs[i]);
	}
	if (sampleCount == 0) {
		pen_move(g_touch.x, g_touch.y);
	}
}

static void continue_eyedropper(void)
{
	if (g_touch.held) {
		pen_pick_color(g_touch.x, g_touch.y);
		return;
	}
	g_sampling = false;
	color_push_recent(g_color);
	tool_select(g_previousTool);
}

static void select_layer(int layerIndex)
{
	if (layer_select(layerIndex)) {
		g_stateDirty = true;
	}
	app_toast("%s", document_current_layer()->props.name);
}

// The actions a button press runs. Pan, eyedropper and the size buttons work while they are held.
static void run_action(int action)
{
	switch (action) {
	case ACTION_UNDO:
		history_undo();
		break;
	case ACTION_REDO:
		history_redo();
		break;
	case ACTION_ERASER:
		rail_toggle_eraser();
		break;
	case ACTION_POPUP:
		ui_toggle_popup();
		break;
	case ACTION_COLOR:
		ui_toggle_sheet(SHEET_COLOR);
		break;
	case ACTION_LAYERS:
		ui_toggle_sheet(SHEET_LAYERS);
		break;
	case ACTION_LIBRARY:
		ui_toggle_sheet(SHEET_BRUSH_LIBRARY);
		break;
	case ACTION_BRUSH_SETTINGS:
		ui_toggle_sheet(SHEET_BRUSH_SETTINGS);
		break;
	case ACTION_MENU:
		ui_toggle_sheet(SHEET_MENU);
		break;
	case ACTION_FIT:
		page_view_toggle_fit();
		break;
	case ACTION_ZOOM_IN:
		page_view_step_zoom(1);
		break;
	case ACTION_ZOOM_OUT:
		page_view_step_zoom(-1);
		break;
	case ACTION_LAYER_UP:
		select_layer(g_document.current + 1);
		break;
	case ACTION_LAYER_DOWN:
		select_layer(g_document.current - 1);
		break;
	case ACTION_RAIL:
		g_railHidden = !g_railHidden;
		page_view_relayout();
		break;
	case ACTION_SAVE:
		session_save(false);
		break;
	}
}

static bool action_opens_ui(int action)
{
	return action == ACTION_MENU || action == ACTION_COLOR || action == ACTION_LAYERS ||
	       action == ACTION_LIBRARY || action == ACTION_BRUSH_SETTINGS || action == ACTION_POPUP;
}

// Over a sheet or the pop-up palette, B goes back and only the actions that open things work.
static void press_button_over_ui(u32 key, int action)
{
	if (key == KEY_B) {
		ui_go_back();
		return;
	}
	if (action_opens_ui(action)) {
		run_action(action);
	}
}

static void press_button(int action, u64 now)
{
	if (action == ACTION_PAN_OR_ERASER) {
		g_panOrEraserUsedToPan = false;
		g_panOrEraserPressTime = now;
		return;
	}
	run_action(action);
}

static void release_pan_or_eraser(u64 now)
{
	bool quickTap = now - g_panOrEraserPressTime < PAN_OR_ERASER_TAP_MS;
	if (!sheet_or_popup_open() && !g_panOrEraserUsedToPan && quickTap) {
		rail_toggle_eraser();
	}
}

static void handle_buttons(u32 pressedKeys, u32 releasedKeys, u64 now)
{
	for (int button = 0; button < BUTTON_COUNT; button++) {
		u32 key = button_map_key(button);
		int action = button_map_action(button);
		if (pressedKeys & key) {
			if (sheet_or_popup_open()) {
				press_button_over_ui(key, action);
				continue;
			}
			press_button(action, now);
		}
		if ((releasedKeys & key) && action == ACTION_PAN_OR_ERASER) {
			release_pan_or_eraser(now);
		}
	}
}

static void repeat_brush_size(u32 heldKeys)
{
	static int heldFrames;
	int direction = 0;
	if (button_map_action_held(heldKeys, ACTION_SIZE_UP)) {
		direction = 1;
	} else if (button_map_action_held(heldKeys, ACTION_SIZE_DOWN)) {
		direction = -1;
	}
	if (direction == 0) {
		heldFrames = 0;
		return;
	}
	bool repeatFrame = heldFrames > SIZE_REPEAT_DELAY_FRAMES && heldFrames % SIZE_REPEAT_INTERVAL_FRAMES == 0;
	if (heldFrames == 0 || repeatFrame) {
		int step = heldFrames > SIZE_FAST_DELAY_FRAMES ? SIZE_FAST_STEP : 1;
		rail_set_brush_size(g_brushRadius + direction * step);
	}
	heldFrames++;
}

static int stronger_axis(int circlePadAxis, int cStickAxis)
{
	return abs(circlePadAxis) > abs(cStickAxis) ? circlePadAxis : cStickAxis;
}

static void pan_with_circle_pads(u32 heldKeys)
{
	static float owedX; // screen px still owed to the pan
	static float owedY;
	circlePosition circlePad = {0, 0};
	circlePosition cStick = {0, 0};
	if (pressure_circle_pad_usable(heldKeys & KEY_TOUCH)) {
		hidCircleRead(&circlePad);
		irrstCstickRead(&cStick);
	}
	int padX = stronger_axis(circlePad.dx, cStick.dx);
	int padY = stronger_axis(circlePad.dy, cStick.dy);
	if (abs(padX) <= CIRCLE_PAD_DEAD_ZONE && abs(padY) <= CIRCLE_PAD_DEAD_ZONE) {
		owedX = 0;
		owedY = 0;
		return;
	}
	// The view moves the way the pad points, so the page moves the other way. The pad's y points up.
	owedX -= padX * CIRCLE_PAD_PAN_SPEED;
	owedY += padY * CIRCLE_PAD_PAN_SPEED;
	int panX = (int)owedX;
	int panY = (int)owedY;
	owedX -= panX;
	owedY -= panY;
	page_view_pan(panX, panY);
}

static void read_touch(u32 pressedKeys, u32 heldKeys, u32 releasedKeys)
{
	touchPosition touch;
	hidTouchRead(&touch);
	g_touch.x = touch.px;
	g_touch.y = touch.py;
	g_touch.held = (heldKeys & KEY_TOUCH) && !(releasedKeys & KEY_TOUCH);
	g_touch.pressed = pressedKeys & KEY_TOUCH;
}

static bool touch_on_page(void)
{
	return g_touch.x >= page_view_left() && g_touch.x < page_view_right();
}

void input_update(u32 pressedKeys, u32 heldKeys, u32 releasedKeys)
{
	u64 now = osGetTime();
	bool panHeld = button_map_action_held(heldKeys, ACTION_PAN_OR_ERASER) ||
	               button_map_action_held(heldKeys, ACTION_PAN);
	bool eyedropperHeld = button_map_action_held(heldKeys, ACTION_EYEDROPPER);

	read_touch(pressedKeys, heldKeys, releasedKeys);
	if (widget_tick_armed()) {
		app_request_bottom_redraw();
	}
	rail_tick();

	// The buttons wait while the stylus pans, draws or picks a colour.
	if (g_panning) {
		continue_pan();
		return;
	}
	if (g_penDown) {
		continue_stroke();
		return;
	}
	if (g_sampling) {
		continue_eyedropper();
		return;
	}

	handle_buttons(pressedKeys, releasedKeys, now);
	if (!sheet_or_popup_open()) {
		repeat_brush_size(heldKeys);
		pan_with_circle_pads(heldKeys);
	}

	widget_begin_input();
	ui_run();
	bool pressFree = widget_end_input();
	bool pressNotTaken = pressFree && !sheet_or_popup_open();
	if (pressNotTaken && touch_on_page()) {
		start_page_touch(g_touch.x, g_touch.y, panHeld, eyedropperHeld);
	}
}
