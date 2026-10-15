#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "png_writer.h"
#include "bytes.h"
#include "color_units.h"
#include "document.h"
#include "undo.h"

#define PNG_IHDR_SIZE 13
#define PNG_IHDR_HEIGHT_OFFSET 4
#define PNG_IHDR_BIT_DEPTH_OFFSET 8
#define PNG_IHDR_COLOR_TYPE_OFFSET 9
#define PNG_BIT_DEPTH 8
#define PNG_COLOR_TYPE_RGB 2
#define PNG_COLOR_TYPE_RGBA 6
#define PNG_FILTER_NONE 0
#define PNG_COMPRESSION_LEVEL 6
#define PNG_STREAM_BUFFER_SIZE 32768

const u8 PNG_SIGNATURE[PNG_SIGNATURE_SIZE] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

static u8 g_deflateOutput[PNG_STREAM_BUFFER_SIZE];
static u8 g_filteredRow[CANVAS_WIDTH * RGBA_PIXEL_BYTES + 1]; // the widest row and its filter byte

static void fill_ihdr(u8 ihdr[PNG_IHDR_SIZE], int width, int height, int colorType)
{
	memset(ihdr, 0, PNG_IHDR_SIZE);
	bytes_write_be32(ihdr, width);
	bytes_write_be32(ihdr + PNG_IHDR_HEIGHT_OFFSET, height);
	ihdr[PNG_IHDR_BIT_DEPTH_OFFSET] = PNG_BIT_DEPTH;
	ihdr[PNG_IHDR_COLOR_TYPE_OFFSET] = colorType;
}

// Writes a chunk (length, type, data, CRC of type and data) at out and returns its end.
static u8 *write_chunk(u8 *out, const char *type, const u8 *data, u32 length)
{
	bytes_write_be32(out, length);
	memcpy(out + PNG_LENGTH_SIZE, type, PNG_CHUNK_TYPE_SIZE);
	if (length) {
		memcpy(out + PNG_CHUNK_HEADER_SIZE, data, length);
	}
	u8 *crcField = out + PNG_CHUNK_HEADER_SIZE + length;
	bytes_write_be32(crcField, crc32(0, out + PNG_LENGTH_SIZE, length + PNG_CHUNK_TYPE_SIZE));
	return out + PNG_CHUNK_OVERHEAD + length;
}

// Every PNG row starts with its filter byte.
static void add_filter_bytes(u8 *raw, const u8 *rgb, int width, int height)
{
	int rowBytes = width * RGB_PIXEL_BYTES;
	for (int y = 0; y < height; y++) {
		u8 *rawRow = raw + (size_t)y * (rowBytes + 1);
		rawRow[0] = PNG_FILTER_NONE;
		memcpy(rawRow + 1, rgb + (size_t)y * rowBytes, (size_t)rowBytes);
	}
}

// The signature, IHDR, one IDAT with the compressed rows and IEND, in one block the caller frees.
static u8 *assemble(int width, int height, const u8 *compressed, uLong compressedSize, size_t *pngSize)
{
	u8 *png = undo_malloc(PNG_SIGNATURE_SIZE + PNG_CHUNK_OVERHEAD + PNG_IHDR_SIZE + PNG_CHUNK_OVERHEAD +
	                      compressedSize + PNG_CHUNK_OVERHEAD);
	if (!png) {
		return NULL;
	}
	u8 ihdr[PNG_IHDR_SIZE];
	fill_ihdr(ihdr, width, height, PNG_COLOR_TYPE_RGB);
	memcpy(png, PNG_SIGNATURE, PNG_SIGNATURE_SIZE);
	u8 *end = write_chunk(png + PNG_SIGNATURE_SIZE, "IHDR", ihdr, PNG_IHDR_SIZE);
	end = write_chunk(end, "IDAT", compressed, compressedSize);
	end = write_chunk(end, "IEND", NULL, 0);
	*pngSize = end - png;
	return png;
}

u8 *png_encode(const u8 *rgb, int width, int height, size_t *pngSize)
{
	size_t rawSize = (size_t)(width * RGB_PIXEL_BYTES + 1) * height;
	u8 *raw = undo_malloc(rawSize);
	uLong compressedSize = compressBound(rawSize);
	u8 *compressed = undo_malloc(compressedSize);
	u8 *png = NULL;
	if (raw && compressed) {
		add_filter_bytes(raw, rgb, width, height);
		if (compress2(compressed, &compressedSize, raw, rawSize, PNG_COMPRESSION_LEVEL) == Z_OK) {
			png = assemble(width, height, compressed, compressedSize, pngSize);
		}
	}
	free(raw);
	free(compressed);
	return png;
}

static void zip_chunk(Zip *zip, const char *type, const u8 *data, u32 length)
{
	u8 header[PNG_CHUNK_HEADER_SIZE];
	u8 crcBytes[PNG_CHUNK_CRC_SIZE];
	bytes_write_be32(header, length);
	memcpy(header + PNG_LENGTH_SIZE, type, PNG_CHUNK_TYPE_SIZE);
	u32 crc = crc32(0, header + PNG_LENGTH_SIZE, PNG_CHUNK_TYPE_SIZE);
	if (length) {
		crc = crc32(crc, data, length); // crc32 given NULL returns 0, not the running crc
	}
	bytes_write_be32(crcBytes, crc);
	zip_write(zip, header, PNG_CHUNK_HEADER_SIZE);
	zip_write(zip, data, length);
	zip_write(zip, crcBytes, PNG_CHUNK_CRC_SIZE);
}

// Runs deflate on what the stream holds; every full output buffer, and the last one, becomes an IDAT chunk.
static bool deflate_into_zip(Zip *zip, z_stream *stream, int flush)
{
	for (;;) {
		int result = deflate(stream, flush);
		if (result == Z_STREAM_ERROR) {
			return false;
		}
		if (stream->avail_out == 0 || result == Z_STREAM_END) {
			if (stream->avail_out < PNG_STREAM_BUFFER_SIZE) {
				zip_chunk(zip, "IDAT", g_deflateOutput, PNG_STREAM_BUFFER_SIZE - stream->avail_out);
			}
			stream->next_out = g_deflateOutput;
			stream->avail_out = PNG_STREAM_BUFFER_SIZE;
		}
		if (!zip->ok) {
			return false;
		}
		// Finishing runs until the stream ends; otherwise until all input is taken.
		bool done = flush == Z_FINISH ? result == Z_STREAM_END : stream->avail_in == 0;
		if (done) {
			return true;
		}
	}
}

void png_write_zip_entry(Zip *zip, const char *entryName, int width, int height, int channels,
                         PngRowFiller fillRow, const void *context)
{
	zip_begin_entry(zip, entryName, false);
	if (!zip->ok) {
		return;
	}
	z_stream stream;
	memset(&stream, 0, sizeof stream);
	if (deflateInit(&stream, PNG_COMPRESSION_LEVEL) != Z_OK) {
		zip->ok = false;
		return;
	}
	int colorType = channels == RGBA_PIXEL_BYTES ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
	u8 ihdr[PNG_IHDR_SIZE];
	fill_ihdr(ihdr, width, height, colorType);
	zip_write(zip, PNG_SIGNATURE, PNG_SIGNATURE_SIZE);
	zip_chunk(zip, "IHDR", ihdr, PNG_IHDR_SIZE);
	stream.next_out = g_deflateOutput;
	stream.avail_out = PNG_STREAM_BUFFER_SIZE;
	for (int y = 0; y < height && zip->ok; y++) {
		g_filteredRow[0] = PNG_FILTER_NONE;
		fillRow(context, y, g_filteredRow + 1);
		stream.next_in = g_filteredRow;
		stream.avail_in = (uInt)(width * channels + 1);
		if (!deflate_into_zip(zip, &stream, Z_NO_FLUSH)) {
			zip->ok = false;
		}
	}
	if (zip->ok && !deflate_into_zip(zip, &stream, Z_FINISH)) {
		zip->ok = false;
	}
	deflateEnd(&stream);
	zip_chunk(zip, "IEND", NULL, 0);
	zip_end_entry(zip);
}
