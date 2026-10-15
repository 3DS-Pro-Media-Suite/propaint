// The tool rail: tool buttons, the size slider, colour swatches, undo, layers and menu buttons.
#pragma once
#include <stdbool.h>

#define RAIL_WIDTH 48

void rail_set_brush_size(int radius);
void rail_tick(void); // once a frame: counts down the brush size ring
bool rail_size_ring_shown(void); // the page shows the brush size for a moment after it changes
void rail_toggle_eraser(void);
void rail_run(void);
