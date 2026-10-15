// Saving a file so that a failed or cut-off save keeps the old one: the data goes to <path>.tmp, which
// replaces the file only once it is whole. A save cut off between removing the old file and renaming the new
// one leaves only the .tmp; reading gives it its name back.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define SAFE_FILE_PATH_SIZE 128 // a path and ".tmp"

// The temporary file a save goes through.
void safe_file_temp_path(const char *path, char tempPath[SAFE_FILE_PATH_SIZE]);
// Puts the temporary file in place of path. When written is false it deletes the temporary file instead. A
// failed rename keeps the temporary file: it is then the only copy.
bool safe_file_commit(const char *tempPath, const char *path, bool written);
// As safe_file_commit, but a failed rename deletes the temporary file too, so no .tmp is left behind.
bool safe_file_commit_or_delete(const char *tempPath, const char *path, bool written);
// Writes head and then body (bodySize may be 0) through the temporary file. A failed save deletes the
// temporary file, as safe_file_commit_or_delete does.
bool safe_file_write(const char *path, const void *head, size_t headSize, const void *body, size_t bodySize);
// When path is missing and its temporary file is there, the temporary file gets path's name back. True when
// it did.
bool safe_file_recover(const char *path);
// Opens path for reading, after safe_file_recover when the file is missing. NULL when there is none.
FILE *safe_file_open_read(const char *path);
