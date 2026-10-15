// The brush library: the user's brushes and the favourites on the pop-up palette. The engine only sees the
// brush in use; this module keeps the brushes to choose from. The library file is written only when its bytes
// changed.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "brush_engine.h"

#define BRUSH_LIBRARY_MAX 160

// One brush of the library. The library file holds these records as they are in memory.
typedef struct {
	Brush brush;
	u8 favorite; // on the pop-up palette's ring
	u8 size; // radius in canvas px the brush comes with (0 = keep the current size)
	u8 mode; // BRUSH_MODE_* it comes with
	u8 fromKrita; // imported from Krita
} LibraryBrush;

extern LibraryBrush g_library[BRUSH_LIBRARY_MAX];
extern int g_libraryCount;

// Loads the library file. Without a readable one it starts over from the engine's presets and the bundled
// Krita brushes.
void brush_library_init(void);
bool brush_library_save(void);
int brush_library_add(const LibraryBrush *brush); // the new brush's index, -1 when the library is full
void brush_library_delete(int index); // the last brush stays
void brush_library_reset(void); // only the engine's presets
void brush_library_changed(void);
bool brush_library_is_dirty(void); // it has changes its file does not hold yet
void brush_library_mark_saved(void); // the file holds the library as it is now
void brush_library_flush(void); // writes the library if its bytes changed
int brush_library_find(const char *name); // the index of the brush with that name, -1 when there is none
// Puts the brush and its size in the hand of the brush or the eraser, whichever holds one (tools.h). The brush
// also takes its mode. An index out of range is ignored.
void brush_library_select(int index);
bool brush_library_is_edited(void); // the brush in hand differs from its library entry, or has none
