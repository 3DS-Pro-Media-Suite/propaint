#include <stdio.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include "zip_writer.h"
#include "bytes.h"
#include "zip_format.h"

#define FILE_BUFFER_SIZE 65536
#define DEFLATE_BUFFER_SIZE 16384

#define DEFLATE_LEVEL 6
#define DEFLATE_MEMORY_LEVEL 8
// Negative window bits make zlib write raw deflate data, which is what zip stores.
#define DEFLATE_WINDOW_BITS (-MAX_WBITS)

#define ZIP_VERSION 20 // 2.0, the first version with deflate

// MS-DOS date and time, as zip stores them.
#define DOS_HOUR_SHIFT 11
#define DOS_MINUTE_SHIFT 5
#define DOS_SECONDS_STEP 2
#define DOS_YEAR_SHIFT 9
#define DOS_MONTH_SHIFT 5
#define DOS_FIRST_YEAR 80 // 1980, counted from 1900 like struct tm

static u8 g_deflateBuffer[DEFLATE_BUFFER_SIZE];

static void write_crc_and_sizes(u8 *destination, const ZipEntry *entry)
{
	bytes_write_le32(destination + ZIP_CRC_FIELD_OFFSET, entry->crc);
	bytes_write_le32(destination + ZIP_COMPRESSED_SIZE_FIELD_OFFSET, entry->compressedSize);
	bytes_write_le32(destination + ZIP_UNCOMPRESSED_SIZE_FIELD_OFFSET, entry->uncompressedSize);
}

static bool write_all(FILE *file, const void *data, size_t size)
{
	return fwrite(data, 1, size, file) == size;
}

static u16 pack_dos_time(int hour, int minute, int second)
{
	return (u16)((hour << DOS_HOUR_SHIFT) | (minute << DOS_MINUTE_SHIFT) | (second / DOS_SECONDS_STEP));
}

static u16 pack_dos_date(int yearsSince1980, int month, int day)
{
	return (u16)((yearsSince1980 << DOS_YEAR_SHIFT) | (month << DOS_MONTH_SHIFT) | day);
}

bool zip_open(Zip *zip, const char *path)
{
	memset(zip, 0, sizeof *zip);
	zip->file = fopen(path, "wb");
	if (!zip->file) {
		return false;
	}
	setvbuf(zip->file, NULL, _IOFBF, FILE_BUFFER_SIZE);
	zip->ok = true;
	time_t now = time(NULL);
	struct tm *utc = gmtime(&now);
	if (utc && utc->tm_year >= DOS_FIRST_YEAR) {
		zip->dosTime = pack_dos_time(utc->tm_hour, utc->tm_min, utc->tm_sec);
		zip->dosDate = pack_dos_date(utc->tm_year - DOS_FIRST_YEAR, utc->tm_mon + 1, utc->tm_mday);
	} else {
		// 1 January 1980, the earliest date the format can hold.
		zip->dosDate = pack_dos_date(0, 1, 1);
	}
	return true;
}

static void start_deflate(Zip *zip)
{
	memset(&zip->stream, 0, sizeof zip->stream);
	int status = deflateInit2(&zip->stream, DEFLATE_LEVEL, Z_DEFLATED, DEFLATE_WINDOW_BITS,
	                          DEFLATE_MEMORY_LEVEL, Z_DEFAULT_STRATEGY);
	zip->streamActive = status == Z_OK;
	zip->ok = zip->streamActive;
}

void zip_begin_entry(Zip *zip, const char *name, bool compressed)
{
	if (!zip->ok || zip->entryCount >= ZIP_ENTRIES_MAX) {
		zip->ok = false;
		return;
	}
	ZipEntry *entry = &zip->entries[zip->entryCount];
	memset(entry, 0, sizeof *entry);
	snprintf(entry->name, sizeof entry->name, "%s", name);
	entry->method = compressed ? ZIP_METHOD_DEFLATED : ZIP_METHOD_STORED;
	entry->localHeaderOffset = (u32)ftell(zip->file);
	size_t nameLength = strlen(entry->name);

	// Crc and sizes stay zero until zip_end_entry fills them in.
	u8 header[ZIP_LOCAL_HEADER_SIZE] = {0};
	bytes_write_le32(header, ZIP_LOCAL_SIGNATURE);
	bytes_write_le16(header + ZIP_LOCAL_VERSION_NEEDED_OFFSET, ZIP_VERSION);
	bytes_write_le16(header + ZIP_LOCAL_METHOD_OFFSET, entry->method);
	bytes_write_le16(header + ZIP_LOCAL_TIME_OFFSET, zip->dosTime);
	bytes_write_le16(header + ZIP_LOCAL_DATE_OFFSET, zip->dosDate);
	bytes_write_le16(header + ZIP_LOCAL_NAME_LENGTH_OFFSET, (u16)nameLength);
	bool headerWritten = write_all(zip->file, header, ZIP_LOCAL_HEADER_SIZE);
	zip->ok = headerWritten && write_all(zip->file, entry->name, nameLength);
	if (compressed && zip->ok) {
		start_deflate(zip);
	}
}

// Runs deflate until it has no more output for the given flush mode.
static void deflate_to_file(Zip *zip, int flush)
{
	ZipEntry *entry = &zip->entries[zip->entryCount];
	int status;
	do {
		zip->stream.next_out = g_deflateBuffer;
		zip->stream.avail_out = sizeof g_deflateBuffer;
		status = deflate(&zip->stream, flush);
		size_t outputSize = sizeof g_deflateBuffer - zip->stream.avail_out;
		if (outputSize && !write_all(zip->file, g_deflateBuffer, outputSize)) {
			zip->ok = false;
		}
		entry->compressedSize += outputSize;
	} while (zip->ok && zip->stream.avail_out == 0 && status != Z_STREAM_END);
}

void zip_write(Zip *zip, const void *data, size_t size)
{
	if (!zip->ok || !size) {
		return;
	}
	ZipEntry *entry = &zip->entries[zip->entryCount];
	entry->crc = crc32(entry->crc, data, size);
	entry->uncompressedSize += size;
	if (entry->method == ZIP_METHOD_STORED) {
		if (!write_all(zip->file, data, size)) {
			zip->ok = false;
		}
		entry->compressedSize += size;
		return;
	}
	zip->stream.next_in = (Bytef *)data;
	zip->stream.avail_in = size;
	deflate_to_file(zip, Z_NO_FLUSH);
}

// Seeks back to the entry's local header and fills in its crc and sizes.
static void patch_local_header(Zip *zip, const ZipEntry *entry)
{
	long entryEnd = ftell(zip->file);
	u8 crcAndSizes[ZIP_CRC_AND_SIZES_SIZE];
	write_crc_and_sizes(crcAndSizes, entry);
	long crcPosition = entry->localHeaderOffset + ZIP_LOCAL_CRC_OFFSET;
	zip->ok = fseek(zip->file, crcPosition, SEEK_SET) == 0 &&
	          write_all(zip->file, crcAndSizes, ZIP_CRC_AND_SIZES_SIZE) &&
	          fseek(zip->file, entryEnd, SEEK_SET) == 0;
}

void zip_end_entry(Zip *zip)
{
	// The deflate state is freed even after an error.
	if (zip->streamActive) {
		if (zip->ok) {
			zip->stream.next_in = NULL;
			zip->stream.avail_in = 0;
			deflate_to_file(zip, Z_FINISH);
		}
		deflateEnd(&zip->stream);
		zip->streamActive = false;
	}
	if (!zip->ok) {
		return;
	}
	patch_local_header(zip, &zip->entries[zip->entryCount]);
	zip->entryCount++;
}

void zip_add_file(Zip *zip, const char *name, const void *data, size_t size, bool compressed)
{
	zip_begin_entry(zip, name, compressed);
	zip_write(zip, data, size);
	zip_end_entry(zip);
}

static void write_central_header(Zip *zip, const ZipEntry *entry)
{
	size_t nameLength = strlen(entry->name);
	u8 header[ZIP_CENTRAL_HEADER_SIZE] = {0};
	bytes_write_le32(header, ZIP_CENTRAL_SIGNATURE);
	bytes_write_le16(header + ZIP_CENTRAL_VERSION_MADE_BY_OFFSET, ZIP_VERSION);
	bytes_write_le16(header + ZIP_CENTRAL_VERSION_NEEDED_OFFSET, ZIP_VERSION);
	bytes_write_le16(header + ZIP_CENTRAL_METHOD_OFFSET, entry->method);
	bytes_write_le16(header + ZIP_CENTRAL_TIME_OFFSET, zip->dosTime);
	bytes_write_le16(header + ZIP_CENTRAL_DATE_OFFSET, zip->dosDate);
	write_crc_and_sizes(header + ZIP_CENTRAL_CRC_OFFSET, entry);
	bytes_write_le16(header + ZIP_CENTRAL_NAME_LENGTH_OFFSET, (u16)nameLength);
	bytes_write_le32(header + ZIP_CENTRAL_LOCAL_HEADER_OFFSET, entry->localHeaderOffset);
	bool headerWritten = write_all(zip->file, header, ZIP_CENTRAL_HEADER_SIZE);
	zip->ok = headerWritten && write_all(zip->file, entry->name, nameLength);
}

static void write_end_record(Zip *zip, u32 directoryOffset)
{
	u32 directoryEnd = (u32)ftell(zip->file);
	u8 record[ZIP_END_RECORD_SIZE] = {0};
	bytes_write_le32(record, ZIP_END_SIGNATURE);
	bytes_write_le16(record + ZIP_END_DISK_ENTRY_COUNT_OFFSET, zip->entryCount);
	bytes_write_le16(record + ZIP_END_TOTAL_ENTRY_COUNT_OFFSET, zip->entryCount);
	bytes_write_le32(record + ZIP_END_DIRECTORY_SIZE_OFFSET, directoryEnd - directoryOffset);
	bytes_write_le32(record + ZIP_END_DIRECTORY_OFFSET_OFFSET, directoryOffset);
	zip->ok = zip->ok && write_all(zip->file, record, ZIP_END_RECORD_SIZE);
}

static void write_central_directory(Zip *zip)
{
	u32 directoryOffset = (u32)ftell(zip->file);
	for (int i = 0; i < zip->entryCount && zip->ok; i++) {
		write_central_header(zip, &zip->entries[i]);
	}
	write_end_record(zip, directoryOffset);
}

bool zip_close(Zip *zip)
{
	if (zip->ok) {
		write_central_directory(zip);
	}
	if (zip->file && fclose(zip->file) != 0) {
		zip->ok = false;
	}
	return zip->ok;
}
