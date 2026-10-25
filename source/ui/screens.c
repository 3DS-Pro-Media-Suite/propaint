#include <string.h>
#include "screens.h"
#include "app_state.h"
#include "brush_sheets.h"
#include "color.h"
#include "document.h"
#include "draw.h"
#include "icons.h"
#include "input.h"
#include "mascot.h"
#include "page_view.h"
#include "pen.h"
#include "rail.h"
#include "render.h"
#include "session.h"
#include "thumbnails.h"
#include "ui.h"
#include "widgets.h"

// Overlays on the bottom screen.
#define SIZE_RING_SHADOW_ALPHA 120
#define SIZE_RING_SHADOW_OFFSET 1 // the shadow ring lies just outside the ring
#define SIZE_RING_ALPHA 220
#define LOUPE_DISTANCE 34 // above the stylus, or below it near the top edge
#define LOUPE_MIN_Y 14
#define LOUPE_OUTLINE_RADIUS 15
#define LOUPE_RIM_RADIUS 13.5f
#define LOUPE_COLOR_RADIUS 11.5f
#define TOAST_HEIGHT 20
#define TOAST_Y 216
#define TOAST_Y_WITH_SHEET 188 // above a sheet's button bar
#define TOAST_TEXT_Y 2
#define TOAST_COLOR RGB(12, 12, 14)

// The top screen: the page at half size and the layer panel on the rail's side.
#define LAYER_PANEL_WIDTH (TOP_SCREEN_WIDTH - TOP_IMAGE_WIDTH)
#define FRAMEBUFFER_COLUMN_BYTES ((size_t)SCREEN_HEIGHT * FRAMEBUFFER_PIXEL_BYTES)
#define TOP_IMAGE_SCALE (1.0f / TOP_IMAGE_SHRINK)
#define VIEW_CORNERS 4
#define ZOOM_LABEL_MARGIN 4
#define ZOOM_LABEL_Y 224
#define ZOOM_LABEL_COLOR RGB(80, 80, 80)
#define MASCOT_SCALE 2
#define MASCOT_MARGIN 2

// The layer rows in the panel.
#define LAYER_ROWS 8
#define LAYER_ROW_TOP 3
#define LAYER_ROW_HEIGHT 26
#define LAYER_HIGHLIGHT_INSET 1 // the current row and the group bar: from the panel sides and above the row
#define LAYER_GROUP_INDENT 5
#define LAYER_GROUP_BAR_X 2
#define LAYER_GROUP_BAR_WIDTH 2
#define LAYER_THUMBNAIL_X 3
#define LAYER_NAME_GAP 3 // between the thumbnail and the name
#define LAYER_NAME_X (LAYER_THUMBNAIL_X + LAYER_THUMBNAIL_WIDTH + LAYER_NAME_GAP)
#define LAYER_NAME_Y 4
#define LAYER_NAME_WIDTH 40
#define LAYER_LOCKED_NAME_WIDTH (LAYER_NAME_WIDTH - ICON_SIZE) // room for the lock icon
#define LAYER_LOCK_ICON_X (LAYER_NAME_X + LAYER_LOCKED_NAME_WIDTH)
#define LAYER_LOCK_ICON_Y 5
#define LAYER_CLIP_ICON_Y (LAYER_THUMBNAIL_HEIGHT - ICON_SIZE)
#define HIDDEN_LAYER_ALPHA 90

// The brush status under the layer rows: colour, tool and size.
#define STATUS_MARGIN 4
#define STATUS_SEPARATOR_Y 213
#define STATUS_Y 219
#define STATUS_CHIP_SIZE 16
#define STATUS_CHIP_FRAME 1
#define STATUS_SEPARATOR_HEIGHT 1
#define STATUS_TOOL_ICON_X (STATUS_MARGIN + STATUS_CHIP_SIZE + STATUS_MARGIN)

// A screen's two framebuffers take turns, so the one drawn into still holds the picture of two frames ago.
typedef struct {
	const u8 *shown; // the framebuffer on the screen: the last one drawn into. NULL: neither is up to date
	bool otherBehind; // the other framebuffer, the next one drawn into, lacks the shown picture
} ScreenBuffers;

static ScreenBuffers g_bottomBuffers;
static ScreenBuffers g_topBuffers;

static void picture_composed(ScreenBuffers *buffers, const u8 *pixels)
{
	buffers->shown = pixels;
	buffers->otherBehind = true;
}

// The picture stays. The framebuffer to draw into gets it from the shown one, unless it holds it already.
static void picture_kept(ScreenBuffers *buffers, u8 *pixels, size_t bytes)
{
	if (buffers->otherBehind) {
		memcpy(pixels, buffers->shown, bytes);
		buffers->otherBehind = false;
	}
	buffers->shown = pixels;
}

void screens_forget_pictures(void)
{
	g_bottomBuffers.shown = NULL;
	g_topBuffers.shown = NULL;
}

// The brush size, around the stylus or in the middle of the drawing area.
static void draw_size_ring(Surface *screen)
{
	u32 ringColor = g_tool == TOOL_ERASER ? COLOR_WARNING : COLOR_WHITE;
	float radius = g_brushRadius * page_view_current()->zoom;
	float ringX = g_penDown ? pen_stylus_x() + 0.5f : page_view_center_x();
	float ringY = g_penDown ? pen_stylus_y() + 0.5f : SCREEN_HEIGHT / 2;
	draw_ring(screen, ringX, ringY, radius + SIZE_RING_SHADOW_OFFSET, COLOR_BLACK, SIZE_RING_SHADOW_ALPHA);
	draw_ring(screen, ringX, ringY, radius, ringColor, SIZE_RING_ALPHA);
}

// The colour under the stylus while it picks one.
static void draw_loupe(Surface *screen)
{
	int loupeX = pen_stylus_x();
	int stylusY = pen_stylus_y();
	bool nearTopEdge = stylusY - LOUPE_DISTANCE < LOUPE_MIN_Y;
	int loupeY = nearTopEdge ? stylusY + LOUPE_DISTANCE : stylusY - LOUPE_DISTANCE;
	draw_disc(screen, loupeX, loupeY, LOUPE_OUTLINE_RADIUS, COLOR_BLACK);
	draw_disc(screen, loupeX, loupeY, LOUPE_RIM_RADIUS, COLOR_WHITE);
	draw_disc(screen, loupeX, loupeY, LOUPE_COLOR_RADIUS, draw_color(g_color));
}

static void draw_toast(Surface *screen)
{
	const char *text = app_toast_text();
	int toastWidth = draw_text_width(FONT_SMALL, text) + 2 * TOAST_PADDING;
	int centerX = (int)page_view_center_x();
	int toastY = ui_current_sheet() != SHEET_NONE ? TOAST_Y_WITH_SHEET : TOAST_Y;
	draw_rounded_rect(screen, centerX - toastWidth / 2, toastY, toastWidth, TOAST_HEIGHT, TOAST_COLOR);
	draw_text(screen, FONT_SMALL, centerX, toastY + TOAST_TEXT_Y, COLOR_TEXT, ALIGN_CENTER, "%s", text);
}

static void draw_bottom_full(Surface *screen)
{
	brush_sheet_reset_preview_budget();
	memcpy(screen->pixels, page_view_bottom_image(), BOTTOM_FRAMEBUFFER_BYTES);
	if (ui_current_sheet() == SHEET_NONE) {
		if (g_penDown || rail_size_ring_shown()) {
			draw_size_ring(screen);
		}
		if (g_sampling) {
			draw_loupe(screen);
		}
	}
	g_uiSurface = screen;
	ui_run();
	g_uiSurface = NULL;
	if (app_toast_shown()) {
		draw_toast(screen);
	}
}

// The bottom screen is composed again only when something can have changed it (input, a toast, a stroke, a
// render); drawing the sheets and the palette is slow, so otherwise the picture is kept.
static void draw_bottom(Surface *screen)
{
	if (app_bottom_redraw_pending() || !g_bottomBuffers.shown) {
		draw_bottom_full(screen);
		app_bottom_redraw_done();
		picture_composed(&g_bottomBuffers, screen->pixels);
	} else {
		picture_kept(&g_bottomBuffers, screen->pixels, BOTTOM_FRAMEBUFFER_BYTES);
	}
}

// The part of the page the bottom screen shows, outlined on the top image.
static void draw_view_outline(Surface *screen, int pageX)
{
	float cornerX[VIEW_CORNERS];
	float cornerY[VIEW_CORNERS];
	int left = page_view_left();
	int right = page_view_right() - 1;
	int bottom = SCREEN_HEIGHT - 1;
	int screenX[VIEW_CORNERS] = {left, right, right, left};
	int screenY[VIEW_CORNERS] = {0, 0, bottom, bottom};
	for (int k = 0; k < VIEW_CORNERS; k++) {
		page_view_screen_to_canvas(screenX[k], screenY[k], &cornerX[k], &cornerY[k]);
		cornerX[k] = pageX + cornerX[k] * TOP_IMAGE_SCALE;
		cornerY[k] *= TOP_IMAGE_SCALE;
	}
	for (int k = 0; k < VIEW_CORNERS; k++) {
		int next = (k + 1) % VIEW_CORNERS;
		// A black copy 1 px down and right keeps the line visible on any page.
		draw_line(screen, cornerX[k] + 1, cornerY[k] + 1, cornerX[next] + 1, cornerY[next] + 1, COLOR_BLACK);
		draw_line(screen, cornerX[k], cornerY[k], cornerX[next], cornerY[next], COLOR_ACCENT);
	}
}

static void draw_layer_row(Surface *screen, int panelX, int rowY, int layerIndex)
{
	const LayerProps *props = &g_document.layers[layerIndex].props;
	int indent = props->group ? LAYER_GROUP_INDENT : 0;
	int highlightY = rowY - LAYER_HIGHLIGHT_INSET;
	if (layerIndex == g_document.current) {
		draw_fill(screen, panelX + LAYER_HIGHLIGHT_INSET, highlightY,
		          LAYER_PANEL_WIDTH - 2 * LAYER_HIGHLIGHT_INSET, LAYER_ROW_HEIGHT, COLOR_ACCENT_DARK, 255);
	}
	bool groupVisible = !props->group || g_document.groups[props->group].visible;
	if (props->group) {
		u32 barColor = groupVisible ? COLOR_ACCENT : COLOR_DISABLED;
		draw_fill(screen, panelX + LAYER_GROUP_BAR_X, highlightY, LAYER_GROUP_BAR_WIDTH, LAYER_ROW_HEIGHT,
		          barColor, 255);
	}
	int thumbX = panelX + LAYER_THUMBNAIL_X + indent;
	int thumbAlpha = props->visible && groupVisible ? 255 : HIDDEN_LAYER_ALPHA;
	thumbnails_draw(screen, thumbX, rowY, layerIndex, thumbAlpha);
	if (props->clipped) {
		draw_icon(screen, IC_CLIP, thumbX, rowY + LAYER_CLIP_ICON_Y, COLOR_ACCENT);
	}
	char name[LAYER_NAME_SIZE];
	int nameWidth = props->locked ? LAYER_LOCKED_NAME_WIDTH : LAYER_NAME_WIDTH;
	draw_fit_text(name, sizeof name, props->name, nameWidth - indent);
	u32 nameColor = props->visible ? COLOR_TEXT : COLOR_DISABLED;
	draw_text(screen, FONT_SMALL, panelX + LAYER_NAME_X + indent, rowY + LAYER_NAME_Y, nameColor, ALIGN_LEFT,
	          "%s", name);
	if (props->locked) {
		draw_icon(screen, IC_LOCK, panelX + LAYER_LOCK_ICON_X, rowY + LAYER_LOCK_ICON_Y, COLOR_DIM);
	}
}

// The rows around the current layer, top layer first.
static void draw_layer_stack(Surface *screen, int panelX)
{
	int currentRow = g_document.layerCount - 1 - g_document.current;
	int firstRow = currentRow - LAYER_ROWS / 2;
	if (firstRow > g_document.layerCount - LAYER_ROWS) {
		firstRow = g_document.layerCount - LAYER_ROWS;
	}
	if (firstRow < 0) {
		firstRow = 0;
	}
	for (int row = firstRow; row < g_document.layerCount && row < firstRow + LAYER_ROWS; row++) {
		int layerIndex = g_document.layerCount - 1 - row;
		int rowY = LAYER_ROW_TOP + (row - firstRow) * LAYER_ROW_HEIGHT;
		draw_layer_row(screen, panelX, rowY, layerIndex);
	}
}

static void draw_brush_status(Surface *screen, int panelX)
{
	static const int toolIcons[] = {
	        [TOOL_BRUSH] = IC_BRUSH,
	        [TOOL_ERASER] = IC_ERASER,
	        [TOOL_EYEDROPPER] = IC_PICKER,
	        [TOOL_MOVE] = IC_MOVE,
	};
	int left = panelX + STATUS_MARGIN;
	draw_fill(screen, left, STATUS_SEPARATOR_Y, LAYER_PANEL_WIDTH - 2 * STATUS_MARGIN,
	          STATUS_SEPARATOR_HEIGHT, COLOR_WIDGET, 255);
	draw_fill(screen, left, STATUS_Y, STATUS_CHIP_SIZE, STATUS_CHIP_SIZE, draw_color(g_color), 255);
	int frameSize = STATUS_CHIP_SIZE + 2 * STATUS_CHIP_FRAME;
	draw_frame(screen, left - STATUS_CHIP_FRAME, STATUS_Y - STATUS_CHIP_FRAME, frameSize, frameSize,
	           COLOR_WIDGET_LIGHT);
	draw_icon(screen, toolIcons[g_tool], panelX + STATUS_TOOL_ICON_X, STATUS_Y, COLOR_TEXT);
	int right = panelX + LAYER_PANEL_WIDTH - STATUS_MARGIN;
	draw_text(screen, FONT_SMALL, right, STATUS_Y, COLOR_DIM, ALIGN_RIGHT, "%d", g_brushRadius);
}

// The zoom in percent, in the page's bottom corner away from the panel.
static void draw_zoom_label(Surface *screen, int pageX)
{
	int labelX = g_leftHanded ? pageX + TOP_IMAGE_WIDTH - ZOOM_LABEL_MARGIN : pageX + ZOOM_LABEL_MARGIN;
	int align = g_leftHanded ? ALIGN_RIGHT : ALIGN_LEFT;
	int percent = (int)(page_view_current()->zoom * 100 + 0.5f);
	draw_text(screen, FONT_SMALL, labelX, ZOOM_LABEL_Y, ZOOM_LABEL_COLOR, align, "%d%%", percent);
}

// The mascot stands in the page's bottom corner next to the panel.
static void draw_mascot(Surface *screen, int pageX)
{
	int leftCornerX = pageX + MASCOT_MARGIN;
	int rightCornerX = pageX + TOP_IMAGE_WIDTH - MASCOT_MARGIN - mascot_frame_size() * MASCOT_SCALE;
	int mascotX = g_leftHanded ? leftCornerX : rightCornerX;
	int mascotY = SCREEN_HEIGHT - 1 - MASCOT_FEET * MASCOT_SCALE;
	mascot_draw(screen->pixels, screen->width, SCREEN_HEIGHT, mascotX, mascotY, MASCOT_SCALE);
}

static void draw_top_full(Surface *screen)
{
	// The layer panel sits on the side of the rail.
	int panelX = g_leftHanded ? 0 : TOP_IMAGE_WIDTH;
	int pageX = g_leftHanded ? LAYER_PANEL_WIDTH : 0;
	draw_reset_clip();
	memcpy(screen->pixels + pageX * FRAMEBUFFER_COLUMN_BYTES, page_view_top_image(),
	       TOP_IMAGE_WIDTH * FRAMEBUFFER_COLUMN_BYTES);
	draw_fill(screen, panelX, 0, LAYER_PANEL_WIDTH, SCREEN_HEIGHT, COLOR_RAIL, 255);
	if (page_view_zoom_step() != ZOOM_STEP_FIT || page_view_current()->rotation != 0) {
		draw_view_outline(screen, pageX);
	}
	draw_layer_stack(screen, panelX);
	draw_brush_status(screen, panelX);
	if (page_view_zoom_step() != ZOOM_STEP_FIT) {
		draw_zoom_label(screen, pageX);
	}
	if (!g_mascotHidden) {
		draw_mascot(screen, pageX);
	}
}

// What draw_top_full shows besides the page image, the thumbnails and the mascot.
typedef struct {
	View view;
	int zoomStep;
	int viewLeft;
	int viewRight;
	int layerCount;
	int currentLayer;
	int tool;
	int brushRadius;
	u8 color[3];
	u8 leftHanded;
	u8 mascotHidden;

	struct {
		char name[LAYER_NAME_SIZE];
		u8 visible;
		u8 locked;
		u8 clipped;
		u8 group;
		u8 groupVisible;
	} layers[MAX_LAYERS];
} TopScreenState;

static bool top_state_changed(void)
{
	static TopScreenState lastState;
	TopScreenState state;
	memset(&state, 0, sizeof state); // memcmp compares the padding and the unused layers too
	state.view = *page_view_current();
	state.zoomStep = page_view_zoom_step();
	state.viewLeft = page_view_left();
	state.viewRight = page_view_right();
	state.layerCount = g_document.layerCount;
	state.currentLayer = g_document.current;
	state.tool = g_tool;
	state.brushRadius = g_brushRadius;
	memcpy(state.color, g_color, sizeof state.color);
	state.leftHanded = g_leftHanded;
	state.mascotHidden = g_mascotHidden;
	for (int i = 0; i < g_document.layerCount; i++) {
		const LayerProps *props = &g_document.layers[i].props;
		memcpy(state.layers[i].name, props->name, sizeof state.layers[i].name);
		state.layers[i].visible = props->visible;
		state.layers[i].locked = props->locked;
		state.layers[i].clipped = props->clipped;
		state.layers[i].group = props->group;
		state.layers[i].groupVisible = g_document.groups[props->group].visible;
	}
	if (memcmp(&state, &lastState, sizeof state) == 0) {
		return false;
	}
	memcpy(&lastState, &state, sizeof state);
	return true;
}

// The top screen is composed again only when something on it changed; otherwise the picture is kept.
static void draw_top(Surface *screen)
{
	bool stateChanged = top_state_changed();
	bool mascotChanged = !g_mascotHidden && mascot_changed();
	bool imageChanged = page_view_take_top_changed();
	if (imageChanged || stateChanged || mascotChanged || !g_topBuffers.shown) {
		draw_top_full(screen);
		picture_composed(&g_topBuffers, screen->pixels);
	} else {
		picture_kept(&g_topBuffers, screen->pixels, TOP_FRAMEBUFFER_BYTES);
	}
}

// Toasts, armed buttons, the size ring and brush previews ask for their own redraws.
static bool bottom_screen_busy(u32 pressedKeys, u32 releasedKeys, bool rendered)
{
	bool keysChanged = pressedKeys | releasedKeys;
	bool widgetHeld = g_touch.held && widget_is_dragging();
	bool stylusBusy = g_penDown || input_panning() || g_sampling;
	return keysChanged || widgetHeld || stylusBusy || session_pending_job() != SESSION_JOB_NONE || rendered;
}

void screens_draw(u32 pressedKeys, u32 releasedKeys)
{
	static bool toastShownLastFrame;
	Surface bottomSurface = {gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), BOTTOM_SCREEN_WIDTH};
	Surface topSurface = {gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), TOP_SCREEN_WIDTH};
	bool rendered = page_view_take_rendered();
	bool busy = bottom_screen_busy(pressedKeys, releasedKeys, rendered);
	bool toastShown = app_toast_shown();
	if (busy || toastShown != toastShownLastFrame) {
		app_request_bottom_redraw(); // and one more after it stops, for the last change
	}
	toastShownLastFrame = toastShown;
	draw_bottom(&bottomSurface);
	draw_top(&topSurface);
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}
