// Writes a zip file one entry at a time. Entry data is streamed and either stored or deflated.
// The file must be seekable: each local header gets its crc and sizes when its entry ends.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <zlib.h>

#define ZIP_ENTRIES_MAX 48
#define ZIP_NAME_SIZE 80

typedef struct {
	char name[ZIP_NAME_SIZE];
	u32 crc;
	u32 compressedSize;
	u32 uncompressedSize;
	u32 localHeaderOffset;
	u16 method;
} ZipEntry;

typedef struct {
	FILE *file;
	ZipEntry entries[ZIP_ENTRIES_MAX];
	int entryCount;
	z_stream stream;
	bool streamActive; // stream holds deflate state that deflateEnd must free
	bool ok; // false after any error; later calls then do nothing
	u16 dosTime;
	u16 dosDate;
} Zip;

// Creates the file at path. Returns false when it cannot be opened.
bool zip_open(Zip *zip, const char *path);
void zip_begin_entry(Zip *zip, const char *name, bool compressed);
// Appends data to the entry started by zip_begin_entry.
void zip_write(Zip *zip, const void *data, size_t size);
void zip_end_entry(Zip *zip);
void zip_add_file(Zip *zip, const char *name, const void *data, size_t size, bool compressed);
// Writes the central directory and closes the file. Returns false if any step failed.
bool zip_close(Zip *zip);
