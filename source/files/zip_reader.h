// Reads a zip file one entry at a time. Only the central directory and the data of the entries asked for are
// read, so a large archive needs little memory.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define ZIP_READER_NAME_SIZE 128 // a longer entry name is cut short

typedef struct {
	char name[ZIP_READER_NAME_SIZE];
	size_t nameLength; // of the name as kept
	u16 method;
	u32 compressedSize;
	u32 uncompressedSize;
	u32 localHeaderOffset;
} ZipReaderEntry;

typedef struct {
	FILE *file;
	u8 *directory; // the central directory
	u32 directorySize;
	u32 entryCount;
	u32 nextEntry;
	u32 nextEntryOffset; // where the next entry starts in the directory
} ZipReader;

// Reads the central directory of an open file. False when the file is not a readable zip.
bool zip_reader_open(ZipReader *zip, FILE *file);
// The next entry of the directory. False after the last one or at a damaged one.
bool zip_reader_next(ZipReader *zip, ZipReaderEntry *entry);
// The entry's data, stored or inflated, in a new buffer; size gets its length. Inflated data is followed by a
// NUL. NULL on a read error, damaged data or another compression method. Caller frees.
u8 *zip_reader_read(ZipReader *zip, const ZipReaderEntry *entry, size_t *size);
void zip_reader_close(ZipReader *zip); // frees the directory; the file stays open
