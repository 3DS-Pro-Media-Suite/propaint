#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "page_view.h"
#include "app_state.h"
#include "document.h"
#include "math_util.h"
#include "rail.h"
#include "render.h"
#include "thumbnails.h"
#include "ui.h"
#include "widgets.h"

#define PAGE_CENTER_X (CANVAS_WIDTH / 2.0f)
#define PAGE_CENTER_Y (CANVAS_HEIGHT / 2.0f)
#define START_ZOOM 0.5f // start-up replaces it with the fit zoom
#define FIRST_FIXED_STEP (ZOOM_STEP_FIT + 1)
#define ZOOM_TOLERANCE 1e-4f
#define FIT_SNAP_RATIO 1.02f // a zoom this close above the fit zoom snaps to fit

// For this long after a zoom, rotation or mirror change the view renders at low resolution.
#define GESTURE_LOW_RES_MS 120
#define GESTURE_LOW_RES_STEP 3 // one sample per 3 x 3 pixels
#define DRAG_LOW_RES_STEP 2 // the top image while a widget holds the stylus

// After a gesture the full-resolution pass renders a few columns at a time, for part of each frame.
#define FULL_RES_COLUMNS 4
#define FULL_RES_MS_PER_FRAME 8
#define FULL_RES_TICKS_PER_FRAME (SYSCLOCK_ARM11 / (1000 / FULL_RES_MS_PER_FRAME))

#define TOP_REDRAW_MARGIN 1 // top-image px around a redrawn rect, for the half-size rounding

#define QUARTER_TURN (TWO_PI / 4)
#define ROTATION_SNAP 0.07f // radians from a quarter turn that snap to it

static const float g_zoomSteps[ZOOM_STEP_COUNT] = {0, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 6, 8, 12, 16};

static View g_view = {.centerX = PAGE_CENTER_X, .centerY = PAGE_CENTER_Y, .zoom = START_ZOOM};
static int g_zoomStep = ZOOM_STEP_FIT;
static int g_zoomStepBeforeFit = ZOOM_STEP_ONE; // where toggling fit off returns to

static u8 *g_bottomImage; // the rendered view
static u8 *g_topImage; // the whole page at half size
static bool g_viewDirty = true;
static bool g_topDirty = true;
static bool g_hasDirtyRect;
static IRect g_dirtyRect; // in bottom-screen pixels

static bool g_renderedThisFrame; // page_view_render_pending drew something this frame
static u64 g_gestureTime; // when zoom, rotation or mirror last changed
static bool g_fullResPending; // the bottom screen still shows a low-res render
static int g_fullResColumn; // the full-resolution pass over that render has reached this column
static bool g_topLowRes; // the top screen shows a low-res render made while a widget held the stylus
static bool g_topImageChanged = true; // the top image or a thumbnail has changes the top screen lacks
static IRect g_topRedrawRect; // canvas rect the top screen redraws once the stroke ends
static bool g_topRedrawPending; // g_topRedrawRect is waiting to be redrawn

bool page_view_init(void)
{
	g_bottomImage = malloc(BOTTOM_FRAMEBUFFER_BYTES);
	g_topImage = malloc(TOP_FRAMEBUFFER_BYTES);
	if (!g_bottomImage || !g_topImage) {
		return false;
	}
	memset(g_bottomImage, 0, BOTTOM_FRAMEBUFFER_BYTES);
	memset(g_topImage, 0, TOP_FRAMEBUFFER_BYTES);
	return true;
}

void page_view_exit(void)
{
	free(g_bottomImage);
	free(g_topImage);
}

const u8 *page_view_bottom_image(void)
{
	return g_bottomImage;
}

const u8 *page_view_top_image(void)
{
	return g_topImage;
}

void page_view_stack_changed(void)
{
	g_topDirty = true;
	g_viewDirty = true;
	thumbnails_mark_all();
	g_unsaved = true;
}

void page_view_look_changed(bool updateThumbnail)
{
	g_topDirty = true;
	g_viewDirty = true;
	if (updateThumbnail) {
		thumbnails_mark_layer(g_document.current);
	}
	g_unsaved = true;
}

void page_view_redraw_all(void)
{
	g_viewDirty = true;
	g_topDirty = true;
	thumbnails_mark_all();
}

void page_view_redraw_top_rect(void)
{
	g_topRedrawPending = true;
}

bool page_view_take_rendered(void)
{
	bool rendered = g_renderedThisFrame;
	g_renderedThisFrame = false;
	return rendered;
}

bool page_view_take_top_changed(void)
{
	bool changed = g_topImageChanged;
	g_topImageChanged = false;
	return changed;
}

int page_view_left(void)
{
	bool railOnLeft = !g_railHidden && !g_leftHanded;
	return railOnLeft ? RAIL_WIDTH : 0;
}

int page_view_right(void)
{
	bool railOnRight = !g_railHidden && g_leftHanded;
	return railOnRight ? BOTTOM_SCREEN_WIDTH - RAIL_WIDTH : BOTTOM_SCREEN_WIDTH;
}

float page_view_center_x(void)
{
	return (page_view_left() + page_view_right()) * 0.5f;
}

float page_view_zoom_of_step(int step)
{
	if (step != ZOOM_STEP_FIT) {
		return g_zoomSteps[step];
	}
	float widthFit = (page_view_right() - page_view_left()) / (float)CANVAS_WIDTH;
	float heightFit = (float)SCREEN_HEIGHT / CANVAS_HEIGHT;
	return widthFit < heightFit ? widthFit : heightFit;
}

const View *page_view_current(void)
{
	return &g_view;
}

int page_view_zoom_step(void)
{
	return g_zoomStep;
}

static void clamp_view(void)
{
	g_view.centerX = clamp_float(g_view.centerX, 0, CANVAS_WIDTH);
	g_view.centerY = clamp_float(g_view.centerY, 0, CANVAS_HEIGHT);
}

// A zoom, rotation or mirror change: the view renders at low resolution for a moment.
static void gesture_changed_view(void)
{
	g_viewDirty = true;
	g_gestureTime = osGetTime();
}

void page_view_set_zoom_step(int step)
{
	g_zoomStep = clamp_int(step, 0, ZOOM_STEP_COUNT - 1);
	g_view.zoom = page_view_zoom_of_step(g_zoomStep);
	if (g_zoomStep == ZOOM_STEP_FIT) {
		g_view.centerX = PAGE_CENTER_X;
		g_view.centerY = PAGE_CENTER_Y;
	}
	clamp_view();
	gesture_changed_view();
}

// g_view is centred on the drawing area, between the rail and the screen edge. The engine's mapping centres
// a view on a W x H screen, so the drawing area is described as a screen twice its centre wide.
static int view_screen_width(void)
{
	return (int)(2 * page_view_center_x());
}

// The view as render() wants it: centred on the whole bottom screen.
static View screen_view(void)
{
	View view = g_view;
	render_screen_to_canvas(&g_view, view_screen_width(), SCREEN_HEIGHT, BOTTOM_SCREEN_WIDTH / 2,
	                        SCREEN_HEIGHT / 2, &view.centerX, &view.centerY);
	return view;
}

// The view the top image is rendered with: the whole page at half size.
static View top_image_view(void)
{
	View view = {.centerX = PAGE_CENTER_X, .centerY = PAGE_CENTER_Y, .zoom = 1.0f / TOP_IMAGE_SHRINK};
	return view;
}

static void render_bottom_rect(const View *view, int x0, int y0, int x1, int y1)
{
	render(view, g_bottomImage, BOTTOM_SCREEN_WIDTH, SCREEN_HEIGHT, x0, y0, x1, y1);
}

static void render_top_rect(const View *view, int x0, int y0, int x1, int y1)
{
	render(view, g_topImage, TOP_IMAGE_WIDTH, SCREEN_HEIGHT, x0, y0, x1, y1);
}

void page_view_mark_dirty(IRect canvasRect)
{
	if (canvasRect.x1 <= canvasRect.x0) {
		return;
	}
	rect_union(&g_topRedrawRect, canvasRect);
	IRect screenRect = render_canvas_rect_on_screen(&g_view, view_screen_width(), SCREEN_HEIGHT, canvasRect);
	if (screenRect.x0 < 0) {
		screenRect.x0 = 0;
	}
	if (screenRect.y0 < 0) {
		screenRect.y0 = 0;
	}
	if (screenRect.x1 > BOTTOM_SCREEN_WIDTH) {
		screenRect.x1 = BOTTOM_SCREEN_WIDTH;
	}
	if (screenRect.y1 > SCREEN_HEIGHT) {
		screenRect.y1 = SCREEN_HEIGHT;
	}
	if (screenRect.x1 <= screenRect.x0 || screenRect.y1 <= screenRect.y0) {
		return;
	}
	if (!g_hasDirtyRect) {
		g_dirtyRect = screenRect;
		g_hasDirtyRect = true;
		return;
	}
	rect_union(&g_dirtyRect, screenRect);
}

// Right after a gesture the whole view renders at low resolution, else at full resolution.
static void render_whole_view(const View *view, int left, int right, bool gestureRecent)
{
	if (gestureRecent) {
		render_lowres(view, g_bottomImage, BOTTOM_SCREEN_WIDTH, SCREEN_HEIGHT, GESTURE_LOW_RES_STEP);
		g_fullResPending = true;
		g_fullResColumn = left;
	} else {
		render_bottom_rect(view, left, 0, right, SCREEN_HEIGHT);
		g_fullResPending = false;
	}
	g_renderedThisFrame = true;
	g_viewDirty = false;
	g_hasDirtyRect = false;
}

static void continue_full_res_pass(const View *view, int left, int right)
{
	u64 startTick = svcGetSystemTick();
	if (g_fullResColumn < left) {
		g_fullResColumn = left;
	}
	while (g_fullResColumn < right && svcGetSystemTick() - startTick < FULL_RES_TICKS_PER_FRAME) {
		int nextColumn = g_fullResColumn + FULL_RES_COLUMNS;
		int endColumn = nextColumn < right ? nextColumn : right;
		render_bottom_rect(view, g_fullResColumn, 0, endColumn, SCREEN_HEIGHT);
		g_fullResColumn = endColumn;
	}
	g_fullResPending = g_fullResColumn < right;
	g_renderedThisFrame = true;
}

static void render_dirty_rect(const View *view, int left, int right)
{
	int dirtyLeft = g_dirtyRect.x0 < left ? left : g_dirtyRect.x0;
	int dirtyRight = g_dirtyRect.x1 > right ? right : g_dirtyRect.x1;
	render_bottom_rect(view, dirtyLeft, g_dirtyRect.y0, dirtyRight, g_dirtyRect.y1);
	g_renderedThisFrame = true;
	g_hasDirtyRect = false;
}

static void render_bottom_pending(void)
{
	int left = page_view_left();
	int right = page_view_right();
	View view = screen_view();
	bool gestureRecent = osGetTime() - g_gestureTime < GESTURE_LOW_RES_MS;
	if (g_viewDirty) {
		render_whole_view(&view, left, right, gestureRecent);
		return;
	}
	if (g_fullResPending && !gestureRecent) {
		continue_full_res_pass(&view, left, right);
	}
	if (g_hasDirtyRect) {
		render_dirty_rect(&view, left, right);
	}
}

static void finish_top_redraw(void)
{
	g_topRedrawPending = false;
	g_topRedrawRect.x1 = g_topRedrawRect.x0;
	g_topImageChanged = true;
}

static void render_whole_top(const View *view)
{
	// While a widget holds the stylus the top image renders at low resolution.
	g_topLowRes = widget_is_dragging();
	if (g_topLowRes) {
		render_lowres(view, g_topImage, TOP_IMAGE_WIDTH, SCREEN_HEIGHT, DRAG_LOW_RES_STEP);
	} else {
		render_top_rect(view, 0, 0, TOP_IMAGE_WIDTH, SCREEN_HEIGHT);
	}
	g_topDirty = false;
	finish_top_redraw();
}

static void render_top_redraw_rect(const View *view)
{
	IRect rect = g_topRedrawRect;
	if (rect.x1 > rect.x0) {
		int x0 = rect.x0 / TOP_IMAGE_SHRINK - TOP_REDRAW_MARGIN;
		int y0 = rect.y0 / TOP_IMAGE_SHRINK - TOP_REDRAW_MARGIN;
		int x1 = (rect.x1 + TOP_IMAGE_SHRINK - 1) / TOP_IMAGE_SHRINK + TOP_REDRAW_MARGIN;
		int y1 = (rect.y1 + TOP_IMAGE_SHRINK - 1) / TOP_IMAGE_SHRINK + TOP_REDRAW_MARGIN;
		render_top_rect(view, x0, y0, x1, y1);
		thumbnails_update_rect(g_document.current, rect);
	}
	finish_top_redraw();
}

static void render_top_pending(void)
{
	View view = top_image_view();
	bool dragEnded = g_topLowRes && !widget_is_dragging();
	if (g_topDirty || dragEnded) {
		render_whole_top(&view);
		return;
	}
	if (g_topRedrawPending) {
		render_top_redraw_rect(&view);
	}
}

void page_view_render_pending(void)
{
	// A sheet covers the page: the bottom screen's renders wait until it closes.
	if (!ui_current_sheet()) {
		render_bottom_pending();
	}
	// The top screen and the thumbnails wait for the stroke to end.
	if (g_penDown) {
		return;
	}
	render_top_pending();
	if (thumbnails_update_marked()) {
		g_topImageChanged = true;
	}
}

// Moves g_view's centre so the content moves by (dx, dy) screen pixels, rotation and mirror included.
// False when the page edge stopped it: the move is then not a whole-pixel shift.
static bool shift_view(int dx, int dy)
{
	int screenWidth = view_screen_width();
	float centerX = screenWidth * 0.5f;
	int centerY = SCREEN_HEIGHT / 2;
	float fromX;
	float fromY;
	float toX;
	float toY;
	render_screen_to_canvas(&g_view, screenWidth, SCREEN_HEIGHT, centerX, centerY, &fromX, &fromY);
	render_screen_to_canvas(&g_view, screenWidth, SCREEN_HEIGHT, centerX + dx, centerY + dy, &toX, &toY);
	float newCenterX = g_view.centerX - (toX - fromX);
	float newCenterY = g_view.centerY - (toY - fromY);
	g_view.centerX = newCenterX;
	g_view.centerY = newCenterY;
	clamp_view();
	return g_view.centerX == newCenterX && g_view.centerY == newCenterY;
}

// The bottom image is column-major, each column stored bottom row first.
static u8 *bottom_image_column(int x)
{
	return g_bottomImage + (size_t)x * SCREEN_HEIGHT * FRAMEBUFFER_PIXEL_BYTES;
}

// Moves the picture between the columns left and right by (dx, dy) pixels.
static void scroll_bottom_image(int dx, int dy, int left, int right)
{
	// The destination rows that have a source row.
	int firstRow = dy > 0 ? dy : 0;
	int endRow = dy < 0 ? SCREEN_HEIGHT + dy : SCREEN_HEIGHT;
	int rowCount = endRow - firstRow;
	int destinationOffset = (SCREEN_HEIGHT - endRow) * FRAMEBUFFER_PIXEL_BYTES;
	int sourceOffset = (SCREEN_HEIGHT - endRow + dy) * FRAMEBUFFER_PIXEL_BYTES;
	for (int i = 0; i < right - left; i++) {
		// Columns are copied against the move, so none is overwritten before it is read.
		int x = dx > 0 ? right - 1 - i : left + i;
		int sourceX = x - dx;
		if (sourceX < left || sourceX >= right) {
			continue;
		}
		memmove(bottom_image_column(x) + destinationOffset, bottom_image_column(sourceX) + sourceOffset,
		        (size_t)rowCount * FRAMEBUFFER_PIXEL_BYTES);
	}
}

static void render_uncovered_strips(int dx, int dy, int left, int right)
{
	View view = screen_view();
	if (dx > 0) {
		render_bottom_rect(&view, left, 0, left + dx, SCREEN_HEIGHT);
	}
	if (dx < 0) {
		render_bottom_rect(&view, right + dx, 0, right, SCREEN_HEIGHT);
	}
	if (dy > 0) {
		render_bottom_rect(&view, left, 0, right, dy);
	}
	if (dy < 0) {
		render_bottom_rect(&view, left, SCREEN_HEIGHT + dy, right, SCREEN_HEIGHT);
	}
}

// Moves the page by whole screen pixels. The picture on the bottom screen is scrolled and only the strips
// that come into view are rendered, because rendering the whole view every frame makes panning lag. The top
// screen's picture does not change, only the frame on it.
void page_view_pan(int dx, int dy)
{
	if (!dx && !dy) {
		return;
	}
	if (!shift_view(dx, dy)) {
		g_viewDirty = true;
		return;
	}
	int left = page_view_left();
	int right = page_view_right();
	bool movedPastArea = abs(dx) >= right - left || abs(dy) >= SCREEN_HEIGHT;
	if (g_viewDirty || g_hasDirtyRect || movedPastArea) {
		g_viewDirty = true;
		return;
	}
	if (g_fullResPending) {
		// The columns already at full resolution move with the picture.
		g_fullResColumn = clamp_int(g_fullResColumn + dx, left, right);
	}
	scroll_bottom_image(dx, dy, left, right);
	render_uncovered_strips(dx, dy, left, right);
	g_renderedThisFrame = true;
}

void page_view_screen_to_canvas(int screenX, int screenY, float *canvasX, float *canvasY)
{
	// + 0.5: the mapping takes the pixel's centre.
	render_screen_to_canvas(&g_view, view_screen_width(), SCREEN_HEIGHT, screenX + 0.5f, screenY + 0.5f,
	                        canvasX, canvasY);
}

void page_view_set_zoom(float zoom)
{
	float fitZoom = page_view_zoom_of_step(ZOOM_STEP_FIT);
	float maxZoom = g_zoomSteps[ZOOM_STEP_COUNT - 1];
	zoom = clamp_float(zoom, fitZoom, maxZoom);
	if (zoom < fitZoom * FIT_SNAP_RATIO) {
		page_view_set_zoom_step(ZOOM_STEP_FIT);
		return;
	}
	int stepBelow = FIRST_FIXED_STEP;
	for (int i = FIRST_FIXED_STEP; i < ZOOM_STEP_COUNT; i++) {
		if (g_zoomSteps[i] <= zoom + ZOOM_TOLERANCE) {
			stepBelow = i;
		}
	}
	g_zoomStep = stepBelow;
	g_view.zoom = zoom;
	clamp_view();
	gesture_changed_view();
}

void page_view_step_zoom(int direction)
{
	float zoom = g_view.zoom;
	if (direction > 0) {
		for (int i = FIRST_FIXED_STEP; i < ZOOM_STEP_COUNT; i++) {
			if (g_zoomSteps[i] > zoom + ZOOM_TOLERANCE) {
				page_view_set_zoom_step(i);
				return;
			}
		}
		return;
	}
	for (int i = ZOOM_STEP_COUNT - 1; i >= FIRST_FIXED_STEP; i--) {
		bool belowZoom = g_zoomSteps[i] < zoom - ZOOM_TOLERANCE;
		if (belowZoom && g_zoomSteps[i] > page_view_zoom_of_step(ZOOM_STEP_FIT)) {
			page_view_set_zoom_step(i);
			return;
		}
	}
	page_view_set_zoom_step(ZOOM_STEP_FIT);
}

void page_view_set_rotation(float radians, bool snap)
{
	float rotation = fmodf(radians, TWO_PI);
	if (rotation < 0) {
		rotation += TWO_PI;
	}
	if (snap) {
		// Stick to 0, 90, 180 and 270 degrees.
		float nearestQuarter = roundf(rotation / QUARTER_TURN) * QUARTER_TURN;
		if (fabsf(rotation - nearestQuarter) < ROTATION_SNAP) {
			rotation = nearestQuarter >= TWO_PI ? 0 : nearestQuarter;
		}
	}
	if (rotation != g_view.rotation) {
		g_view.rotation = rotation;
		gesture_changed_view();
	}
}

void page_view_toggle_mirror(void)
{
	g_view.mirrored = !g_view.mirrored;
	gesture_changed_view();
}

void page_view_toggle_fit(void)
{
	if (g_zoomStep != ZOOM_STEP_FIT) {
		g_zoomStepBeforeFit = g_zoomStep;
		page_view_set_zoom_step(ZOOM_STEP_FIT);
	} else {
		page_view_set_zoom_step(g_zoomStepBeforeFit);
	}
}

void page_view_relayout(void)
{
	if (g_zoomStep == ZOOM_STEP_FIT) {
		page_view_set_zoom_step(ZOOM_STEP_FIT);
	}
	g_viewDirty = true;
}
