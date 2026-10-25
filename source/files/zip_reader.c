#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "zip_reader.h"
#include "bytes.h"
#include "inflate_buffer.h"
#include "zip_format.h"

#define ZIP_END_SEARCH_SIZE 70000 // the end record plus the longest zip comment

// fseek takes a long, so a damaged offset past LONG_MAX fails here instead of wrapping.
static bool seek_to(FILE *file, u64 offset)
{
	return offset <= LONG_MAX && fseek(file, (long)offset, SEEK_SET) == 0;
}

// Finds the end record in the last bytes of the zip and reads where its central directory is.
static bool read_end_record(ZipReader *zip, long fileSize)
{
	long tailSize = fileSize < ZIP_END_SEARCH_SIZE ? fileSize : ZIP_END_SEARCH_SIZE;
	u8 *tail = malloc(tailSize);
	if (!tail) {
		return false;
	}
	if (!seek_to(zip->file, fileSize - tailSize) || fread(tail, 1, tailSize, zip->file) != (size_t)tailSize) {
		free(tail);
		return false;
	}
	long recordOffset = tailSize - ZIP_END_RECORD_SIZE;
	while (recordOffset > 0 && bytes_read_le32(tail + recordOffset) != ZIP_END_SIGNATURE) {
		recordOffset--;
	}
	const u8 *record = tail + recordOffset;
	if (bytes_read_le32(record) != ZIP_END_SIGNATURE) {
		free(tail);
		return false;
	}
	zip->entryCount = bytes_read_le16(record + ZIP_END_TOTAL_ENTRY_COUNT_OFFSET);
	zip->directorySize = bytes_read_le32(record + ZIP_END_DIRECTORY_SIZE_OFFSET);
	u32 directoryOffset = bytes_read_le32(record + ZIP_END_DIRECTORY_OFFSET_OFFSET);
	free(tail);
	if ((u64)directoryOffset + zip->directorySize > (u64)fileSize) {
		return false;
	}
	return seek_to(zip->file, directoryOffset);
}

bool zip_reader_open(ZipReader *zip, FILE *file)
{
	memset(zip, 0, sizeof *zip);
	zip->file = file;
	if (fseek(file, 0, SEEK_END)) {
		return false;
	}
	long fileSize = ftell(file);
	if (fileSize < ZIP_END_RECORD_SIZE || !read_end_record(zip, fileSize)) {
		return false;
	}
	zip->directory = malloc(zip->directorySize);
	if (!zip->directory) {
		return false;
	}
	if (fread(zip->directory, 1, zip->directorySize, file) != zip->directorySize) {
		zip_reader_close(zip);
		return false;
	}
	return true;
}

// The size of a central directory entry: its header, file name, extra field and comment.
static u32 central_entry_size(const u8 *entry)
{
	u16 nameLength = bytes_read_le16(entry + ZIP_CENTRAL_NAME_LENGTH_OFFSET);
	u16 extraLength = bytes_read_le16(entry + ZIP_CENTRAL_EXTRA_LENGTH_OFFSET);
	u16 commentLength = bytes_read_le16(entry + ZIP_CENTRAL_COMMENT_LENGTH_OFFSET);
	return ZIP_CENTRAL_HEADER_SIZE + nameLength + extraLength + commentLength;
}

bool zip_reader_next(ZipReader *zip, ZipReaderEntry *entry)
{
	if (zip->nextEntry >= zip->entryCount) {
		return false;
	}
	if (zip->nextEntryOffset + ZIP_CENTRAL_HEADER_SIZE > zip->directorySize) {
		return false;
	}
	const u8 *header = zip->directory + zip->nextEntryOffset;
	if (bytes_read_le32(header) != ZIP_CENTRAL_SIGNATURE) {
		return false;
	}
	u32 entrySize = central_entry_size(header);
	if (zip->nextEntryOffset + entrySize > zip->directorySize) {
		return false;
	}
	zip->nextEntry++;
	zip->nextEntryOffset += entrySize;
	const u8 *crcAndSizes = header + ZIP_CENTRAL_CRC_OFFSET;
	entry->method = bytes_read_le16(header + ZIP_CENTRAL_METHOD_OFFSET);
	entry->compressedSize = bytes_read_le32(crcAndSizes + ZIP_COMPRESSED_SIZE_FIELD_OFFSET);
	entry->uncompressedSize = bytes_read_le32(crcAndSizes + ZIP_UNCOMPRESSED_SIZE_FIELD_OFFSET);
	entry->localHeaderOffset = bytes_read_le32(header + ZIP_CENTRAL_LOCAL_HEADER_OFFSET);
	u16 nameLength = bytes_read_le16(header + ZIP_CENTRAL_NAME_LENGTH_OFFSET);
	entry->nameLength = nameLength < sizeof entry->name - 1 ? nameLength : sizeof entry->name - 1;
	memcpy(entry->name, header + ZIP_CENTRAL_HEADER_SIZE, entry->nameLength);
	entry->name[entry->nameLength] = 0;
	return true;
}

// The entry's data as it is stored, which follows its local header. NULL on a bad header or a read error.
static u8 *read_stored_data(FILE *file, const ZipReaderEntry *entry)
{
	u8 localHeader[ZIP_LOCAL_HEADER_SIZE];
	if (!seek_to(file, entry->localHeaderOffset) ||
	    fread(localHeader, 1, ZIP_LOCAL_HEADER_SIZE, file) != ZIP_LOCAL_HEADER_SIZE) {
		return NULL;
	}
	if (bytes_read_le32(localHeader) != ZIP_LOCAL_SIGNATURE) {
		return NULL;
	}
	u16 nameLength = bytes_read_le16(localHeader + ZIP_LOCAL_NAME_LENGTH_OFFSET);
	u16 extraLength = bytes_read_le16(localHeader + ZIP_LOCAL_EXTRA_LENGTH_OFFSET);
	u64 dataOffset = (u64)entry->localHeaderOffset + ZIP_LOCAL_HEADER_SIZE + nameLength + extraLength;
	if (!seek_to(file, dataOffset)) {
		return NULL;
	}
	// An empty entry still gets a buffer, so NULL only means failure.
	u32 dataSize = entry->compressedSize;
	u8 *data = malloc(dataSize ? dataSize : 1);
	if (!data) {
		return NULL;
	}
	if (fread(data, 1, dataSize, file) != dataSize) {
		free(data);
		return NULL;
	}
	return data;
}

u8 *zip_reader_read(ZipReader *zip, const ZipReaderEntry *entry, size_t *size)
{
	if (entry->method != ZIP_METHOD_STORED && entry->method != ZIP_METHOD_DEFLATED) {
		return NULL;
	}
	u8 *stored = read_stored_data(zip->file, entry);
	if (!stored || entry->method == ZIP_METHOD_STORED) {
		*size = entry->compressedSize;
		return stored;
	}
	// The directory's inflated size is only the first guess: a damaged one can be too small.
	u8 *inflated = inflate_buffer(stored, entry->compressedSize, INFLATE_RAW, entry->uncompressedSize, size);
	free(stored);
	return inflated;
}

void zip_reader_close(ZipReader *zip)
{
	free(zip->directory);
	zip->directory = NULL;
}
