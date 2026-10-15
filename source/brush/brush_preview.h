// The brush preview: a sample stroke drawn by the real brush engine on a private layer.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "brush_engine.h"

#define BRUSH_PREVIEW_MIN_SIZE 8
#define BRUSH_PREVIEW_MAX_WIDTH 256
#define BRUSH_PREVIEW_MAX_HEIGHT 128

// Draws a sample stroke of a brush; the document is untouched. The result is width x height straight RGBA on
// a transparent background. mode is BRUSH_MODE_*; isSdf draws on an SDF layer. The image stays blank while a
// stroke is active or when the size is outside BRUSH_PREVIEW_*.
void brush_preview_draw(const Brush *brush, int mode, const u8 color[3], bool isSdf, u8 *rgba, int width,
                        int height);
