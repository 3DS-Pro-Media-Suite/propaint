#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "kpp_file.h"
#include "bytes.h"
#include "inflate_buffer.h"
#include "png_writer.h"

#define ZTXT_METHOD_SIZE 1
#define ITXT_FLAGS_SIZE 2 // compression flag and compression method

// The text chunk keyword of a preset. Its size counts the NUL that ends it.
#define PRESET_KEYWORD "preset"
#define PRESET_KEYWORD_SIZE sizeof(PRESET_KEYWORD)

// Inflates zlib data, whose inflated size the chunk does not give, into a new NUL-terminated string.
static char *inflate_to_string(const u8 *source, size_t sourceSize)
{
	size_t length;
	return (char *)inflate_buffer(source, sourceSize, INFLATE_ZLIB, INFLATE_SIZE_UNKNOWN, &length);
}

// A new NUL-terminated copy of the bytes from start to end.
static char *copy_to_string(const u8 *start, const u8 *end)
{
	char *text = malloc(end - start + 1);
	if (text) {
		memcpy(text, start, end - start);
		text[end - start] = 0;
	}
	return text;
}

// Skips a NUL-terminated field. The result is past end when the field has no NUL.
static const u8 *skip_text_field(const u8 *field, const u8 *end)
{
	while (field < end && *field) {
		field++;
	}
	return field + 1;
}

// zTXt text: a compression method byte, then zlib data.
static char *read_ztxt_text(const u8 *text, const u8 *end)
{
	if (text >= end) {
		return NULL;
	}
	const u8 *compressed = text + ZTXT_METHOD_SIZE;
	return inflate_to_string(compressed, end - compressed);
}

// iTXt text: compression flag, compression method, language tag, translated keyword, then the text.
static char *read_itxt_text(const u8 *text, const u8 *end)
{
	if (end - text < ITXT_FLAGS_SIZE) {
		return NULL;
	}
	bool compressed = text[0];
	const u8 *languageTag = text + ITXT_FLAGS_SIZE;
	const u8 *translatedKeyword = skip_text_field(languageTag, end);
	const u8 *content = skip_text_field(translatedKeyword, end);
	if (content > end) {
		return NULL;
	}
	if (compressed) {
		return inflate_to_string(content, end - content);
	}
	return copy_to_string(content, end);
}

char *kpp_file_preset_xml(const u8 *file, size_t fileSize)
{
	if (fileSize < PNG_SIGNATURE_SIZE || memcmp(file, PNG_SIGNATURE, PNG_SIGNATURE_SIZE)) {
		return NULL;
	}
	size_t offset = PNG_SIGNATURE_SIZE;
	while (offset + PNG_CHUNK_OVERHEAD <= fileSize) {
		u32 dataSize = bytes_read_be32(file + offset);
		const u8 *type = file + offset + PNG_LENGTH_SIZE;
		const u8 *data = file + offset + PNG_CHUNK_HEADER_SIZE;
		if (dataSize > fileSize - offset - PNG_CHUNK_OVERHEAD) {
			break;
		}
		bool isText = !memcmp(type, "tEXt", PNG_CHUNK_TYPE_SIZE);
		bool isCompressedText = !memcmp(type, "zTXt", PNG_CHUNK_TYPE_SIZE);
		bool isInternationalText = !memcmp(type, "iTXt", PNG_CHUNK_TYPE_SIZE);
		bool isTextChunk = isText || isCompressedText || isInternationalText;
		if (isTextChunk && dataSize > PRESET_KEYWORD_SIZE &&
		    !memcmp(data, PRESET_KEYWORD, PRESET_KEYWORD_SIZE)) {
			const u8 *text = data + PRESET_KEYWORD_SIZE;
			const u8 *end = data + dataSize;
			if (isText) {
				return copy_to_string(text, end);
			}
			if (isCompressedText) {
				return read_ztxt_text(text, end);
			}
			return read_itxt_text(text, end);
		}
		offset += PNG_CHUNK_OVERHEAD + dataSize;
	}
	return NULL;
}
