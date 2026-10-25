// The stylus pressure: the touch chip's own session (tpressure), the filter on its readings, and when the
// Circle Pad can be read again after a pressure stroke.
#pragma once
#include <stdbool.h>

#define PRESSURE_NONE (-1.0f) // no pressure pen: the engine's pressure sensors then count as full

bool pressure_wanted(void); // pressure pen wanted (opens tpressure); on unless switched off
void pressure_restore_wanted(bool wanted); // the saved wish, before pressure_open

void pressure_open(void); // at start-up, when it is wanted
void pressure_close(void);
void pressure_pause(void); // the touch report goes back to the OS layout (keyboard, HOME, sleep)
void pressure_poll(void); // once a frame before the input: the chip reports pressure only during strokes
void pressure_set_wanted(bool wanted);
void pressure_stroke_begin(void); // the stylus touched the page: the filter starts over
void pressure_stroke_end(void);
void pressure_read(void); // once a frame during a stroke
float pressure_current(void); // the filtered pressure 0..1 for the stroke, or PRESSURE_NONE
bool pressure_settled(void); // the first readings are in: the stroke can be laid down
bool pressure_circle_pad_usable(bool touchHeld); // once a frame; false while the touch report replaces it
