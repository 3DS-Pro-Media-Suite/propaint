// PNG files: the format's signature and chunk layout, and 8-bit PNGs written into memory or streamed into a
// zip entry.
#pragma once
#include <3ds.h>
#include <stddef.h>
#include "zip_writer.h"

// A PNG is the signature, then chunks: a length, a type, the data and a CRC of the type and the data.
#define PNG_SIGNATURE_SIZE 8
#define PNG_LENGTH_SIZE 4
#define PNG_CHUNK_TYPE_SIZE 4
#define PNG_CHUNK_CRC_SIZE 4
#define PNG_CHUNK_HEADER_SIZE (PNG_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE)
#define PNG_CHUNK_OVERHEAD (PNG_CHUNK_HEADER_SIZE + PNG_CHUNK_CRC_SIZE)

extern const u8 PNG_SIGNATURE[PNG_SIGNATURE_SIZE];

// Fills out with row y of an image: width * channels bytes.
typedef void (*PngRowFiller)(const void *context, int y, u8 *out);

// An 8-bit RGB PNG of a row-major RGB image, in memory. NULL when out of memory. Caller frees.
u8 *png_encode(const u8 *rgb, int width, int height, size_t *pngSize);
// A PNG as a new stored zip entry, filled a row at a time by fillRow, so no whole image is held in memory.
// channels is RGB_PIXEL_BYTES or RGBA_PIXEL_BYTES (straight alpha); width is at most CANVAS_WIDTH.
void png_write_zip_entry(Zip *zip, const char *entryName, int width, int height, int channels,
                         PngRowFiller fillRow, const void *context);
