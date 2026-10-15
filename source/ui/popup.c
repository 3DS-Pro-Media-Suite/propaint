#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "popup.h"
#include "app_state.h"
#include "brush_library.h"
#include "brush_sheets.h"
#include "color.h"
#include "color_units.h"
#include "document.h"
#include "draw.h"
#include "math_util.h"
#include "page_view.h"
#include "ui.h"
#include "widgets.h"

// The top of a ring in radians: -PI / 2 rounded, as the exact float shifts some spots by a pixel. y points
// down, so angles go clockwise.
#define TOP_ANGLE (-1.5708f)
#define QUARTER_TURN_DEGREES 90 // hue 0 is at the top of the hue ring

// Radii in px from the palette's centre.
#define PALETTE_RADIUS 113
#define CLOSE_DISTANCE (PALETTE_RADIUS + 4) // a tap further out closes the palette
#define ROTATION_RING_RADIUS 105
#define ROTATION_GRAB_DISTANCE (ROTATION_RING_RADIUS - 9) // a tap from here out turns the canvas
#define ROTATION_TICK_COUNT 8
#define TICK_ANGLE 0.785398f // TWO_PI / ROTATION_TICK_COUNT rounded; the exact float moves two ticks a pixel
#define ROTATION_TICK_HALF_LENGTH 3
#define KNOB_RADIUS 5.5f // the rotation and zoom knobs
#define KNOB_OUTLINE_RADIUS 7
#define BRUSH_RING_RADIUS 82
#define BRUSH_SPOTS_MIN 6 // fewer favourites keep the spacing of six
#define BRUSH_SPOT_RADIUS 12.5f
#define BRUSH_SPOT_OUTLINE_RADIUS 14.5f
#define BRUSH_SPOT_HIT_RADIUS 15
#define RECENT_RING_RADIUS 62
#define RECENT_SPOT_RADIUS 4.5f
#define RECENT_SPOT_OUTLINE_RADIUS 6
#define RECENT_SPOT_HIT_RADIUS 7
#define HUE_RING_INNER_RADIUS 38
#define HUE_RING_OUTER_RADIUS 53
#define HUE_RING_GRAB_MARGIN 3
#define HUE_MARKER_RADIUS 4
#define SQUARE_HALF 24 // half the side of the colour square
#define SQUARE_SIZE (2 * SQUARE_HALF)
#define SQUARE_GRAB_MARGIN 2
#define SQUARE_MARKER_RADIUS 3
#define MARKER_OUTLINE 1 // the hue and square markers: a white ring inside a black one

#define BACKDROP_ALPHA 236
#define FAVORITES_MAX 12
#define EMPTY_HINT_RAISE 6 // lifts the hint text onto the brush ring
#define DOUBLE_TAP_MS 350

// The hue ring is drawn from an RGBA image made on first use.
#define HUE_IMAGE_RADIUS (HUE_RING_OUTER_RADIUS + 1)
#define HUE_IMAGE_SIZE (2 * HUE_IMAGE_RADIUS)
#define HUE_IMAGE_PIXEL_BYTES RGBA_PIXEL_BYTES

// A brush spot shows the middle of the brush's RGB icon.
#define BRUSH_ICON_SHOWN 18
#define BRUSH_ICON_BORDER ((BRUSH_ICON_SIZE - BRUSH_ICON_SHOWN) / 2)

// Buttons in the drawing area's corners. Bit 0 of a corner picks the right side, bit 1 the bottom.
#define CORNER_BUTTON_WIDTH 56
#define CORNER_BUTTON_HEIGHT 26
#define CORNER_MARGIN 4
#define CORNER_LABEL_Y 5
#define CORNER_RIGHT_BIT 1
#define CORNER_BOTTOM_BIT 2
#define ZOOM_ONE_TOLERANCE 1e-3f

// The zoom slider runs along the drawing area's edge away from the rail, with x at the track's centre.
#define ZOOM_SLIDER_TOP 40
#define ZOOM_SLIDER_BOTTOM (SCREEN_HEIGHT - 40)
#define ZOOM_SLIDER_PADDING 6 // around the track, for its background and for touches
#define ZOOM_TRACK_INSET 12 // from the drawing area's edge
#define ZOOM_TRACK_WIDTH 2
#define ZOOM_BACKGROUND_WIDTH 20
#define ZOOM_TOUCH_HALF_WIDTH 12

enum {
	CORNER_MIRROR,
	CORNER_ROTATION,
	CORNER_ZOOM_ONE,
	CORNER_FIT,
	CORNER_COUNT
};

typedef struct {
	int centerX;
	int centerY;
	int favorites[FAVORITES_MAX]; // library indices
	int favoriteCount;
	int recentCount;
	int zoomTrackX;
	float minZoom; // the zoom slider's range
	float maxZoom;
} Palette;

static const char *const CORNER_LABELS[CORNER_COUNT] = {"Mirror", "0 deg", "100%", "Fit"};

static u8 *g_hueRingImage;
static u64 g_lastTapTime;
static int g_lastTapZone;

static float wrap_degrees(float degrees)
{
	if (degrees < 0) {
		return degrees + FULL_TURN_DEGREES;
	}
	if (degrees >= FULL_TURN_DEGREES) {
		return degrees - FULL_TURN_DEGREES;
	}
	return degrees;
}

// The angle of a spot on a ring of spotCount spots, from the top, clockwise.
static float spot_angle(float index, int spotCount)
{
	return TOP_ANGLE + index * TWO_PI / spotCount;
}

static int brush_spot_count(const Palette *palette)
{
	return palette->favoriteCount < BRUSH_SPOTS_MIN ? BRUSH_SPOTS_MIN : palette->favoriteCount;
}

static void layout_palette(Palette *palette)
{
	palette->centerX = (int)page_view_center_x();
	palette->centerY = SCREEN_HEIGHT / 2;
	palette->favoriteCount = 0;
	for (int i = 0; i < g_libraryCount && palette->favoriteCount < FAVORITES_MAX; i++) {
		if (g_library[i].favorite) {
			palette->favorites[palette->favoriteCount] = i;
			palette->favoriteCount++;
		}
	}
	palette->recentCount = g_recentColorCount < RECENT_COLORS_MAX ? g_recentColorCount : RECENT_COLORS_MAX;
	palette->zoomTrackX =
	        g_leftHanded ? page_view_left() + ZOOM_TRACK_INSET : page_view_right() - ZOOM_TRACK_INSET;
	palette->minZoom = page_view_zoom_of_step(ZOOM_STEP_FIT);
	palette->maxZoom = g_zoomSteps[ZOOM_STEP_COUNT - 1];
}

static void corner_position(int corner, int *x, int *y)
{
	*x = corner & CORNER_RIGHT_BIT ? page_view_right() - CORNER_BUTTON_WIDTH - CORNER_MARGIN
	                               : page_view_left() + CORNER_MARGIN;
	*y = corner & CORNER_BOTTOM_BIT ? SCREEN_HEIGHT - CORNER_BUTTON_HEIGHT - CORNER_MARGIN : CORNER_MARGIN;
}

static bool touch_in_corner(int corner)
{
	int x;
	int y;
	corner_position(corner, &x, &y);
	return g_touch.x >= x && g_touch.x < x + CORNER_BUTTON_WIDTH && g_touch.y >= y &&
	       g_touch.y < y + CORNER_BUTTON_HEIGHT;
}

static void press_corner(int corner)
{
	if (corner == CORNER_MIRROR) {
		page_view_toggle_mirror();
		app_toast(g_view.mirrored ? "Mirrored" : "Not mirrored");
	} else if (corner == CORNER_ROTATION) {
		page_view_set_rotation(0, false);
	} else if (corner == CORNER_ZOOM_ONE) {
		page_view_set_zoom_step(ZOOM_STEP_ONE);
	} else {
		page_view_set_zoom_step(ZOOM_STEP_FIT);
	}
}

static bool touch_in_zoom_slider(const Palette *palette)
{
	int left = palette->zoomTrackX - ZOOM_TOUCH_HALF_WIDTH;
	int right = palette->zoomTrackX + ZOOM_TOUCH_HALF_WIDTH;
	int top = ZOOM_SLIDER_TOP - ZOOM_SLIDER_PADDING;
	int bottom = ZOOM_SLIDER_BOTTOM + ZOOM_SLIDER_PADDING;
	return g_touch.x >= left && g_touch.x < right && g_touch.y >= top && g_touch.y < bottom;
}

// The double-tappable control under a press: the zoom slider, the rotation ring or WIDGET_NONE.
static int press_zone(const Palette *palette, float distance)
{
	if (touch_in_zoom_slider(palette)) {
		return WIDGET_PALETTE_ZOOM;
	}
	if (distance > CLOSE_DISTANCE) {
		return WIDGET_NONE;
	}
	if (distance >= ROTATION_GRAB_DISTANCE) {
		return WIDGET_PALETTE_ROTATION;
	}
	return WIDGET_NONE;
}

// A second tap on the zoom slider or the rotation ring resets it instead of dragging.
static bool is_double_tap(int zone)
{
	u64 now = osGetTime();
	bool doubleTap = zone && zone == g_lastTapZone && now - g_lastTapTime < DOUBLE_TAP_MS;
	g_lastTapZone = zone;
	g_lastTapTime = now;
	return doubleTap;
}

static void reset_zone(int zone)
{
	if (zone == WIDGET_PALETTE_ZOOM) {
		page_view_set_zoom_step(ZOOM_STEP_ONE);
	} else {
		page_view_set_rotation(0, false);
	}
	g_lastTapZone = WIDGET_NONE; // a third tap is not another double tap
}

static bool pick_favorite(const Palette *palette)
{
	for (int k = 0; k < palette->favoriteCount; k++) {
		float angle = spot_angle(k, brush_spot_count(palette));
		float offsetX = palette->centerX + cosf(angle) * BRUSH_RING_RADIUS - g_touch.x;
		float offsetY = palette->centerY + sinf(angle) * BRUSH_RING_RADIUS - g_touch.y;
		if (offsetX * offsetX + offsetY * offsetY < BRUSH_SPOT_HIT_RADIUS * BRUSH_SPOT_HIT_RADIUS) {
			brush_library_select(palette->favorites[k]);
			app_toast("%s", g_library[palette->favorites[k]].brush.name);
			ui_close_popup();
			return true;
		}
	}
	return false;
}

static bool pick_recent_color(const Palette *palette)
{
	for (int k = 0; k < palette->recentCount; k++) {
		float angle = spot_angle(k + 0.5f, RECENT_COLORS_MAX);
		float offsetX = palette->centerX + cosf(angle) * RECENT_RING_RADIUS - g_touch.x;
		float offsetY = palette->centerY + sinf(angle) * RECENT_RING_RADIUS - g_touch.y;
		if (offsetX * offsetX + offsetY * offsetY < RECENT_SPOT_HIT_RADIUS * RECENT_SPOT_HIT_RADIUS) {
			color_set(g_recentColors[k]);
			return true;
		}
	}
	return false;
}

// (dx, dy) is the touch relative to the palette's centre. Returns false when the press has done its whole
// job, true when dragging may follow.
static bool take_press(const Palette *palette, float dx, float dy, float distance)
{
	for (int corner = 0; corner < CORNER_COUNT; corner++) {
		if (touch_in_corner(corner)) {
			press_corner(corner);
			return false;
		}
	}
	int zone = press_zone(palette, distance);
	if (is_double_tap(zone)) {
		reset_zone(zone);
		return false;
	}
	if (zone == WIDGET_PALETTE_ZOOM) {
		widget_capture(WIDGET_PALETTE_ZOOM);
		return true;
	}
	if (distance > CLOSE_DISTANCE) {
		ui_close_popup();
		return false;
	}
	if (zone == WIDGET_PALETTE_ROTATION) {
		widget_capture(WIDGET_PALETTE_ROTATION);
		return true;
	}
	if (pick_favorite(palette) || pick_recent_color(palette)) {
		return false;
	}
	bool onHueRing = distance >= HUE_RING_INNER_RADIUS - HUE_RING_GRAB_MARGIN &&
	                 distance <= HUE_RING_OUTER_RADIUS + HUE_RING_GRAB_MARGIN;
	if (onHueRing) {
		widget_capture(WIDGET_PALETTE_HUE_RING);
	} else if (fabsf(dx) <= SQUARE_HALF + SQUARE_GRAB_MARGIN &&
	           fabsf(dy) <= SQUARE_HALF + SQUARE_GRAB_MARGIN) {
		widget_capture(WIDGET_PALETTE_COLOR_SQUARE);
	}
	return true;
}

static void drag_zoom(const Palette *palette)
{
	int sliderHeight = ZOOM_SLIDER_BOTTOM - ZOOM_SLIDER_TOP;
	float position = clamp_unit(1 - (g_touch.y - ZOOM_SLIDER_TOP) / (float)sliderHeight);
	page_view_set_zoom(palette->minZoom * expf(position * logf(palette->maxZoom / palette->minZoom)));
}

static void drag_color(float dx, float dy)
{
	if (widget_captured(WIDGET_PALETTE_HUE_RING)) {
		g_pickerHue = wrap_degrees(atan2f(dy, dx) * RADIANS_TO_DEGREES_ROUNDED + QUARTER_TURN_DEGREES);
	} else {
		float saturation = (dx + SQUARE_HALF) / SQUARE_SIZE;
		float darkness = (dy + SQUARE_HALF) / SQUARE_SIZE;
		g_pickerSaturation = clamp_unit(saturation);
		g_pickerValue = 1 - clamp_unit(darkness);
	}
	color_set_from_picker();
}

static void take_input(const Palette *palette)
{
	float dx = g_touch.x - palette->centerX;
	float dy = g_touch.y - palette->centerY;
	float distance = sqrtf(dx * dx + dy * dy);
	if (widget_take_press() && !take_press(palette, dx, dy, distance)) {
		return;
	}
	if (!g_touch.held) {
		return;
	}
	if (widget_captured(WIDGET_PALETTE_ZOOM)) {
		drag_zoom(palette);
	} else if (widget_captured(WIDGET_PALETTE_ROTATION)) {
		page_view_set_rotation(atan2f(dx, -dy), true); // 0 at the top, clockwise
	} else if (widget_captured(WIDGET_PALETTE_HUE_RING) || widget_captured(WIDGET_PALETTE_COLOR_SQUARE)) {
		drag_color(dx, dy);
	}
}

static void draw_backdrop(const Palette *palette)
{
	for (int y = palette->centerY - PALETTE_RADIUS - 1; y <= palette->centerY + PALETTE_RADIUS + 1; y++) {
		int rowOffset = y - palette->centerY;
		float halfWidth = sqrtf(fmaxf(0, (float)PALETTE_RADIUS * PALETTE_RADIUS - rowOffset * rowOffset));
		int halfPixels = (int)halfWidth;
		draw_fill(g_uiSurface, palette->centerX - halfPixels, y, 2 * halfPixels, 1, COLOR_RAIL,
		          BACKDROP_ALPHA);
	}
}

// The knob shows the canvas rotation: 0 at the top, clockwise.
static void draw_rotation_ring(const Palette *palette)
{
	float centerX = palette->centerX;
	float centerY = palette->centerY;
	draw_ring(g_uiSurface, centerX, centerY, ROTATION_RING_RADIUS, COLOR_WIDGET_LIGHT, 255);
	float tickInner = ROTATION_RING_RADIUS - ROTATION_TICK_HALF_LENGTH;
	float tickOuter = ROTATION_RING_RADIUS + ROTATION_TICK_HALF_LENGTH;
	for (int k = 0; k < ROTATION_TICK_COUNT; k++) {
		float angle = k * TICK_ANGLE;
		draw_line(g_uiSurface, centerX + sinf(angle) * tickInner, centerY - cosf(angle) * tickInner,
		          centerX + sinf(angle) * tickOuter, centerY - cosf(angle) * tickOuter, COLOR_DISABLED);
	}
	float knobX = centerX + sinf(g_view.rotation) * ROTATION_RING_RADIUS;
	float knobY = centerY - cosf(g_view.rotation) * ROTATION_RING_RADIUS;
	draw_disc(g_uiSurface, knobX, knobY, KNOB_OUTLINE_RADIUS, COLOR_RAIL);
	draw_disc(g_uiSurface, knobX, knobY, KNOB_RADIUS,
	          widget_captured(WIDGET_PALETTE_ROTATION) ? COLOR_ACCENT : COLOR_TEXT);
}

static void make_hue_ring_image(void)
{
	g_hueRingImage = malloc((size_t)HUE_IMAGE_SIZE * HUE_IMAGE_SIZE * HUE_IMAGE_PIXEL_BYTES);
	if (!g_hueRingImage) {
		return;
	}
	for (int y = 0; y < HUE_IMAGE_SIZE; y++) {
		for (int x = 0; x < HUE_IMAGE_SIZE; x++) {
			float dx = x + 0.5f - HUE_IMAGE_RADIUS;
			float dy = y + 0.5f - HUE_IMAGE_RADIUS;
			float distance = sqrtf(dx * dx + dy * dy);
			// How far the pixel centre is inside the ring, plus half a pixel, gives its anti-aliased cover.
			float inside = fminf(distance - HUE_RING_INNER_RADIUS, HUE_RING_OUTER_RADIUS - distance);
			float cover = fminf(inside + 0.5f, 1);
			u8 *pixel = g_hueRingImage + (y * HUE_IMAGE_SIZE + x) * HUE_IMAGE_PIXEL_BYTES;
			float hue = wrap_degrees(atan2f(dy, dx) * RADIANS_TO_DEGREES_ROUNDED + QUARTER_TURN_DEGREES);
			color_hsv_to_rgb(hue, 1, 1, pixel);
			pixel[RGBA_ALPHA_OFFSET] = cover <= 0 ? 0 : (u8)(cover * 255);
		}
	}
}

static void draw_hue_ring(const Palette *palette)
{
	if (!g_hueRingImage) {
		make_hue_ring_image();
	}
	if (g_hueRingImage) {
		int left = palette->centerX - HUE_IMAGE_RADIUS;
		int top = palette->centerY - HUE_IMAGE_RADIUS;
		for (int y = 0; y < HUE_IMAGE_SIZE; y++) {
			for (int x = 0; x < HUE_IMAGE_SIZE; x++) {
				const u8 *pixel = g_hueRingImage + (y * HUE_IMAGE_SIZE + x) * HUE_IMAGE_PIXEL_BYTES;
				int alpha = pixel[RGBA_ALPHA_OFFSET];
				if (alpha) {
					draw_pixel(g_uiSurface, left + x, top + y, RGB(pixel[0], pixel[1], pixel[2]), alpha);
				}
			}
		}
	}
	// The marker on the picked hue, in the middle of the ring.
	float angle = (g_pickerHue - QUARTER_TURN_DEGREES) * DEGREES_TO_RADIANS_ROUNDED;
	float middleRadius = (HUE_RING_INNER_RADIUS + HUE_RING_OUTER_RADIUS) * 0.5f;
	float markerX = palette->centerX + cosf(angle) * middleRadius;
	float markerY = palette->centerY + sinf(angle) * middleRadius;
	draw_ring(g_uiSurface, markerX, markerY, HUE_MARKER_RADIUS + MARKER_OUTLINE, COLOR_BLACK, 255);
	draw_ring(g_uiSurface, markerX, markerY, HUE_MARKER_RADIUS, COLOR_WHITE, 255);
}

// Saturation grows to the right, value grows to the top.
static void draw_color_square(const Palette *palette)
{
	u8 hue[3];
	color_hsv_to_rgb(g_pickerHue, 1, 1, hue);
	int left = palette->centerX - SQUARE_HALF;
	int top = palette->centerY - SQUARE_HALF;
	for (int x = 0; x < SQUARE_SIZE; x++) {
		for (int y = 0; y < SQUARE_SIZE; y++) {
			float saturation = x / (SQUARE_SIZE - 1.0f);
			float value = 1 - y / (SQUARE_SIZE - 1.0f);
			u8 rgb[3];
			color_square_rgb(hue, saturation, value, rgb);
			draw_pixel(g_uiSurface, left + x, top + y, RGB(rgb[0], rgb[1], rgb[2]), 255);
		}
	}
	float markerX = left + g_pickerSaturation * (SQUARE_SIZE - 1);
	float markerY = top + (1 - g_pickerValue) * (SQUARE_SIZE - 1);
	draw_ring(g_uiSurface, markerX, markerY, SQUARE_MARKER_RADIUS + MARKER_OUTLINE, COLOR_BLACK, 255);
	draw_ring(g_uiSurface, markerX, markerY, SQUARE_MARKER_RADIUS, COLOR_WHITE, 255);
}

static void draw_recent_colors(const Palette *palette)
{
	for (int k = 0; k < palette->recentCount; k++) {
		float angle = spot_angle(k + 0.5f, RECENT_COLORS_MAX);
		float spotX = palette->centerX + cosf(angle) * RECENT_RING_RADIUS;
		float spotY = palette->centerY + sinf(angle) * RECENT_RING_RADIUS;
		draw_disc(g_uiSurface, spotX, spotY, RECENT_SPOT_OUTLINE_RADIUS, COLOR_WIDGET_LIGHT);
		draw_disc(g_uiSurface, spotX, spotY, RECENT_SPOT_RADIUS, draw_color(g_recentColors[k]));
	}
}

static void draw_brush_icon(int libraryIndex, float spotX, float spotY)
{
	const u8 *icon = brush_sheet_icon(libraryIndex);
	if (!icon) {
		return;
	}
	int left = (int)spotX - BRUSH_ICON_SHOWN / 2;
	int top = (int)spotY - BRUSH_ICON_SHOWN / 2;
	for (int y = 0; y < BRUSH_ICON_SHOWN; y++) {
		for (int x = 0; x < BRUSH_ICON_SHOWN; x++) {
			int iconX = x + BRUSH_ICON_BORDER;
			int iconY = y + BRUSH_ICON_BORDER;
			const u8 *source = icon + (iconY * BRUSH_ICON_SIZE + iconX) * RGB_PIXEL_BYTES;
			draw_pixel(g_uiSurface, left + x, top + y, RGB(source[0], source[1], source[2]), 255);
		}
	}
}

// The spot of the current brush is lit while its settings are unchanged.
static void draw_favorites(const Palette *palette)
{
	bool edited = brush_library_is_edited();
	for (int k = 0; k < palette->favoriteCount; k++) {
		float angle = spot_angle(k, brush_spot_count(palette));
		float spotX = palette->centerX + cosf(angle) * BRUSH_RING_RADIUS;
		float spotY = palette->centerY + sinf(angle) * BRUSH_RING_RADIUS;
		bool current = palette->favorites[k] == g_presetIndex && !edited;
		draw_disc(g_uiSurface, spotX, spotY, BRUSH_SPOT_OUTLINE_RADIUS,
		          current ? COLOR_ACCENT : COLOR_WIDGET_LIGHT);
		draw_disc(g_uiSurface, spotX, spotY, BRUSH_SPOT_RADIUS, COLOR_WHITE);
		draw_brush_icon(palette->favorites[k], spotX, spotY);
	}
	if (!palette->favoriteCount) {
		draw_text(g_uiSurface, FONT_SMALL, palette->centerX,
		          palette->centerY + BRUSH_RING_RADIUS - EMPTY_HINT_RAISE, COLOR_DIM, ALIGN_CENTER,
		          "Star brushes");
	}
}

static bool zoom_is_one(void)
{
	return fabsf(g_view.zoom - 1) < ZOOM_ONE_TOLERANCE;
}

// A lit corner shows a changed setting that a tap resets.
static bool corner_lit(int corner)
{
	if (corner == CORNER_MIRROR) {
		return g_view.mirrored;
	}
	if (corner == CORNER_ROTATION) {
		return g_view.rotation != 0;
	}
	if (corner == CORNER_ZOOM_ONE) {
		return !zoom_is_one();
	}
	return false;
}

static const char *corner_label(int corner)
{
	if (corner == CORNER_ROTATION && g_view.rotation != 0) {
		return draw_format("%d deg", (int)lroundf(g_view.rotation * RADIANS_TO_DEGREES_ROUNDED));
	}
	if (corner == CORNER_ZOOM_ONE && !zoom_is_one()) {
		return draw_format("%d%%", (int)(g_view.zoom * 100 + 0.5f));
	}
	return CORNER_LABELS[corner];
}

static void draw_corners(void)
{
	for (int corner = 0; corner < CORNER_COUNT; corner++) {
		int x;
		int y;
		corner_position(corner, &x, &y);
		bool lit = corner_lit(corner);
		const char *label = corner_label(corner);
		draw_rounded_rect(g_uiSurface, x, y, CORNER_BUTTON_WIDTH, CORNER_BUTTON_HEIGHT,
		                  lit ? COLOR_ACCENT : COLOR_RAIL);
		draw_text(g_uiSurface, FONT_SMALL, x + CORNER_BUTTON_WIDTH / 2, y + CORNER_LABEL_Y,
		          lit ? COLOR_RAIL : COLOR_TEXT, ALIGN_CENTER, "%s", label);
	}
}

// Log scale: each step along the slider multiplies the zoom by the same factor.
static void draw_zoom_slider(const Palette *palette)
{
	int trackX = palette->zoomTrackX;
	int sliderHeight = ZOOM_SLIDER_BOTTOM - ZOOM_SLIDER_TOP;
	draw_rounded_rect(g_uiSurface, trackX - ZOOM_BACKGROUND_WIDTH / 2, ZOOM_SLIDER_TOP - ZOOM_SLIDER_PADDING,
	                  ZOOM_BACKGROUND_WIDTH, sliderHeight + 2 * ZOOM_SLIDER_PADDING, COLOR_RAIL);
	draw_fill(g_uiSurface, trackX - ZOOM_TRACK_WIDTH / 2, ZOOM_SLIDER_TOP, ZOOM_TRACK_WIDTH, sliderHeight,
	          COLOR_WIDGET_LIGHT, 255);
	float position = logf(g_view.zoom / palette->minZoom) / logf(palette->maxZoom / palette->minZoom);
	float knobY = ZOOM_SLIDER_BOTTOM - position * sliderHeight;
	draw_disc(g_uiSurface, trackX, knobY, KNOB_RADIUS,
	          widget_captured(WIDGET_PALETTE_ZOOM) ? COLOR_ACCENT : COLOR_TEXT);
}

void popup_run(void)
{
	Palette palette;
	layout_palette(&palette);
	if (!g_uiSurface) {
		take_input(&palette);
		return;
	}
	draw_backdrop(&palette);
	draw_rotation_ring(&palette);
	draw_hue_ring(&palette);
	draw_color_square(&palette);
	draw_recent_colors(&palette);
	draw_favorites(&palette);
	draw_corners();
	draw_zoom_slider(&palette);
}
