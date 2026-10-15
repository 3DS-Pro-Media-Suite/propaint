// Compositing the document into a framebuffer-shaped image (full and low resolution) or a flattened RGB copy,
// mapping between screen and canvas, and sampling one layer or a group at a canvas pixel.
#pragma once
#include <3ds.h>
#include "document.h"

typedef struct {
	float centerX; // canvas point at the screen centre
	float centerY;
	float zoom; // screen px per canvas px
	float rotation; // canvas rotation on screen, radians, clockwise
	int mirrored; // 1: the canvas is mirrored left-right (applied before the rotation)
} View;

// destination is a framebuffer-shaped BGR8 image of width x height: column-major, 3 bytes per pixel, each
// column from the bottom row up. Draws the screen rectangle x0..x1, y0..y1 (x1 and y1 exclusive).
void render(const View *view, u8 *destination, int width, int height, int x0, int y0, int x1, int y1);
// Interactive redraw: the whole width x height screen at 1/step resolution (one sample per step x step
// block, replicated). Use it while zooming, rotating or panning, then render() when it settles.
void render_lowres(const View *view, u8 *destination, int width, int height, int step);
// Call after changing layers and before a batch of render_layer_sample / render_group_sample calls.
void render_prep(void);
// How far a layer can show past its drawn bounds, in canvas px (outline, shadow, soft edge).
int render_layer_padding(const LayerProps *props);
// A canvas rect grown by padding on every side, kept on the page.
IRect render_pad_on_page(IRect rect, int padding);
// Screen (width x height) -> canvas, with the view's zoom, rotation and mirror. Screen points are in
// pixel-centre coordinates (pass x + 0.5, y + 0.5 for the pixel x, y).
void render_screen_to_canvas(const View *view, int width, int height, float screenX, float screenY, float *u,
                             float *v);
// The screen rectangle (clamped, +1 px) that covers a canvas rect.
IRect render_canvas_rect_on_screen(const View *view, int width, int height, IRect canvasRect);
// Premultiplied colour (0..1) and alpha at a canvas pixel, zoom 1.
void render_layer_sample(const Layer *layer, int x, int y, float color[3], float *alpha);
// The same for the layers bottom..top composited as an isolated group.
void render_group_sample(int bottom, int top, int x, int y, float color[3], float *alpha);
// Premultiplied colour (0..1) and alpha as a straight-alpha RGBA8 pixel. alpha is above 0.
void render_store_straight_rgba(const float color[3], float alpha, u8 out[4]);
// Bilinear, premultiplied: out = {r * a, g * a, b * a} (0..255 x alpha 0..1), a (0..255).
void render_pixels_bilinear(const u8 *pixels, float u, float v, float out[4]);
// Field value in canvas px at canvas point (u, v).
float render_field_bilinear(const s8 *field, float u, float v);
// The flattened drawing on its white paper, scaled to width x height (nearest), as row-major RGB. Caller
// frees.
u8 *render_flattened(int width, int height);
