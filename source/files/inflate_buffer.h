// Inflating deflate data whose inflated size is not known for sure into a new buffer.
#pragma once
#include <3ds.h>
#include <stddef.h>

enum {
	INFLATE_ZLIB, // zlib data with its header, as in PNG text chunks
	INFLATE_RAW // raw deflate data, as zip stores it
};

#define INFLATE_SIZE_UNKNOWN 0

// The data inflated into a new buffer followed by a NUL; inflatedSize gets its length. expectedSize is only
// the first guess (INFLATE_SIZE_UNKNOWN: guessed from sourceSize): bigger output is tried again in a buffer
// twice the size. NULL on damaged data, too much output or no memory. Caller frees.
u8 *inflate_buffer(const u8 *source, size_t sourceSize, int format, size_t expectedSize,
                   size_t *inflatedSize);
