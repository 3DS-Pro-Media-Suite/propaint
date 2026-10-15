#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "zip_reader.h"
#include "bytes.h"
#include "zip_format.h"

#define ZIP_END_SEARCH_SIZE 70000 // the end record plus the longest zip comment
// The first output buffer when the directory gives no inflated size. It doubles on each new attempt,
// because a damaged directory can also give too small a size.
#define INFLATE_GUESS_RATIO 4
#define INFLATE_GUESS_EXTRA 1024
#define INFLATE_ATTEMPTS_MAX 6

// Finds the end record in the last bytes of the zip and reads where its central directory is.
static bool read_end_record(ZipReader *zip, long fileSize)
{
	long tailSize = fileSize < ZIP_END_SEARCH_SIZE ? fileSize : ZIP_END_SEARCH_SIZE;
	u8 *tail = malloc(tailSize);
	if (!tail) {
		return false;
	}
	fseek(zip->file, fileSize - tailSize, SEEK_SET);
	if (fread(tail, 1, tailSize, zip->file) != (size_t)tailSize) {
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
	return fseek(zip->file, directoryOffset, SEEK_SET) == 0;
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
	fseek(file, entry->localHeaderOffset, SEEK_SET);
	if (fread(localHeader, 1, ZIP_LOCAL_HEADER_SIZE, file) != ZIP_LOCAL_HEADER_SIZE) {
		return NULL;
	}
	if (bytes_read_le32(localHeader) != ZIP_LOCAL_SIGNATURE) {
		return NULL;
	}
	u16 nameLength = bytes_read_le16(localHeader + ZIP_LOCAL_NAME_LENGTH_OFFSET);
	u16 extraLength = bytes_read_le16(localHeader + ZIP_LOCAL_EXTRA_LENGTH_OFFSET);
	fseek(file, entry->localHeaderOffset + ZIP_LOCAL_HEADER_SIZE + nameLength + extraLength, SEEK_SET);
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

// Raw deflate data, as zip stores it, inflated into a new NUL-terminated buffer. inflatedSize (from the
// directory) is only the first guess: bigger output is tried again in a buffer twice the size.
static u8 *inflate_entry(const u8 *source, u32 sourceSize, u32 inflatedSize)
{
	size_t capacity = inflatedSize ? (size_t)inflatedSize + 1
	                               : (size_t)sourceSize * INFLATE_GUESS_RATIO + INFLATE_GUESS_EXTRA;
	u8 *output = NULL;
	for (int attempt = 0; attempt < INFLATE_ATTEMPTS_MAX; attempt++) {
		free(output);
		output = malloc(capacity);
		if (!output) {
			return NULL;
		}
		z_stream stream;
		memset(&stream, 0, sizeof stream);
		// Negative window bits tell zlib the data has no zlib header.
		if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
			break;
		}
		stream.next_in = (Bytef *)source;
		stream.avail_in = sourceSize;
		stream.next_out = output;
		stream.avail_out = (uInt)(capacity - 1);
		int result = inflate(&stream, Z_FINISH);
		size_t outputSize = stream.total_out;
		inflateEnd(&stream);
		if (result == Z_STREAM_END) {
			output[outputSize] = 0;
			return output;
		}
		// Z_BUF_ERROR and Z_OK mean the output did not fit.
		if (result != Z_BUF_ERROR && result != Z_OK) {
			break;
		}
		capacity *= 2;
	}
	free(output);
	return NULL;
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
	u8 *inflated = inflate_entry(stored, entry->compressedSize, entry->uncompressedSize);
	free(stored);
	*size = entry->uncompressedSize;
	return inflated;
}

void zip_reader_close(ZipReader *zip)
{
	free(zip->directory);
	zip->directory = NULL;
}
