// Cort, the mascot on the top screen. The app reports events and input, main ticks him once a frame and the
// top screen draws him. Left alone he idles, strolls around and plays small animations.
#pragma once
#include <3ds.h>
#include <stdbool.h>

typedef enum {
	MASCOT_HELLO, // app start, mascot switched on, FTP up: waves
	MASCOT_SAVED, // thumbs up
	MASCOT_EXPORTED, // cheers
	MASCOT_FAILED, // save/export/convert failed, out of memory: hurt, then sad
	MASCOT_SAD, // softer failure (no Wi-Fi): sad
	MASCOT_REFUSED, // nothing to undo/redo, layer locked/hidden, layer limit: shrugs
	MASCOT_IDEA, // merge, convert done: light bulb
	MASCOT_NEW_DRAWING, // new drawing: surprised
	MASCOT_STROKE, // every finished stroke; every STROKES_PER_SWOON-th one plays the love animation
} MascotEvent;

// The first empty row under his shoes: to stand him on a line, draw him at y = line - MASCOT_FEET * scale.
#define MASCOT_FEET 29
#define MASCOT_WANDER_RANGE 120 // in sprite pixels

void mascot_init(void);
void mascot_event(MascotEvent event);
void mascot_input(void); // call when any key or the touch screen is used: keeps him awake, wakes him up
void mascot_tick(void); // once per frame
// pixels is a framebuffer-shaped BGR8 image of width x height (see framebuffer.h); drawing is clipped to it.
// (homeX, homeY) is his home corner. He wanders up to MASCOT_WANDER_RANGE sprite pixels (times scale) from it
// toward the other side of the image, so keep that stretch clear.
void mascot_draw(u8 *pixels, int width, int height, int homeX, int homeY, int scale);
int mascot_frame_size(void); // his frames are square, this many sprite pixels; draw at any integer scale
// He would look different from his last mascot_draw: another frame, facing or spot.
bool mascot_changed(void);
