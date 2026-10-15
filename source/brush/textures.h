// Brush textures: grey PNG tiles a brush repeats over the canvas. Pro Paint brings the ones its Krita brushes
// use (romfs/textures), and more can be put on the SD card in TEXTURES_FOLDER. A brush names its texture by
// file name; "" is the built-in noise.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include "brush_engine.h"

#define TEXTURES_MAX 64

typedef enum {
	TEXTURE_FROM_APP, // Pro Paint's own, from Krita's brushes
	TEXTURE_FROM_SD_CARD,
} TextureSource;

void textures_scan(void); // lists the textures in romfs, then the ones on the SD card
int textures_count(void);
const char *textures_name(int index);
TextureSource textures_source(int index);
int textures_find(const char *name); // its list index, -1 when it is not listed
// The texture's tile, read on first use and kept while it is one of the last few used. NULL for "" (the
// noise) and for a texture that cannot be read.
const TextureTile *textures_tile(const char *name);
// Stores a texture's PNG on the SD card and lists it, unless one by that name is listed. False when it cannot.
bool textures_add_png(const char *name, const u8 *png, size_t size);
