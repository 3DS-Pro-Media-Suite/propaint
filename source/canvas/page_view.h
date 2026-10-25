// The page on screen: the drawing area's layout, zoom, rotation and panning, dirty tracking and rendering
// into the bottom and top buffers.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "document.h"
#include "render.h"

#define ZOOM_STEP_COUNT 12
#define ZOOM_STEP_FIT 0 // fits the page to the drawing area
#define ZOOM_STEP_ONE 3 // zoom 1
#define TOP_IMAGE_SHRINK 2 // the top image shows the whole page at half size
#define TOP_IMAGE_WIDTH (CANVAS_WIDTH / TOP_IMAGE_SHRINK)

bool page_view_init(void); // the rendered images; false when out of memory
void page_view_exit(void);
const u8 *page_view_bottom_image(void); // the view, framebuffer-shaped
const u8 *page_view_top_image(void); // the whole page at half size, TOP_IMAGE_WIDTH columns
// Each returns its flag and clears it: the view rendered something this frame / the top image or a thumbnail
// has changes the top screen lacks.
bool page_view_take_rendered(void);
bool page_view_take_top_changed(void);

// The drawing area's columns on the bottom screen: first column, column after the last, centre.
int page_view_left(void);
int page_view_right(void);
float page_view_center_x(void);
float page_view_zoom_of_step(int step);
const View *page_view_current(void); // centerX/centerY: the canvas point at the centre of the drawing area
int page_view_zoom_step(void);
void page_view_set_zoom_step(int step);
// Any zoom from fit to the largest step. page_view_zoom_step() is then the step below it.
void page_view_set_zoom(float zoom);
void page_view_step_zoom(int direction); // to the next step up (direction > 0) or down from the current zoom
void page_view_toggle_fit(void);
void page_view_set_rotation(float radians, bool snap); // snap: near a quarter turn sticks to it
void page_view_relayout(void); // the drawing area moved or changed size
void page_view_pan(int dx, int dy); // by whole screen pixels
void page_view_screen_to_canvas(int screenX, int screenY, float *canvasX, float *canvasY);
// A changed canvas rect: the bottom screen redraws it next frame, the top screen after
// page_view_redraw_top_rect.
void page_view_mark_dirty(IRect canvasRect);
void page_view_redraw_top_rect(void); // the rects marked dirty go to the top screen, not the whole page
void page_view_toggle_mirror(void);
void page_view_stack_changed(void); // both screens and every thumbnail
// How a layer or a group is composited changed. updateThumbnail: the current layer's thumbnail shows it.
void page_view_look_changed(bool updateThumbnail);
void page_view_redraw_all(void); // both screens and every thumbnail, without marking the drawing unsaved
void page_view_render_pending(void); // once a frame
