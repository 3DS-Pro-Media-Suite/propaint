// The layer thumbnails the layer lists show: each layer at its own size over a transparency checkerboard.
#pragma once
#include <stdbool.h>
#include "document.h"
#include "draw.h"

#define LAYER_THUMBNAIL_WIDTH 32
#define LAYER_THUMBNAIL_HEIGHT 24

void thumbnails_mark_all(void); // every thumbnail is made again
void thumbnails_mark_layer(int layerIndex);
bool thumbnails_update_marked(void); // remakes the marked thumbnails; false when none is marked
// Remakes the pixels of one layer's thumbnail whose cell centre lies in canvasRect.
void thumbnails_update_rect(int layerIndex, IRect canvasRect);
void thumbnails_draw(Surface *surface, int x, int y, int layerIndex, int alpha);
