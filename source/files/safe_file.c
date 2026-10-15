#include "safe_file.h"

#define TEMP_SUFFIX ".tmp"

void safe_file_temp_path(const char *path, char tempPath[SAFE_FILE_PATH_SIZE])
{
	snprintf(tempPath, SAFE_FILE_PATH_SIZE, "%s" TEMP_SUFFIX, path);
}

bool safe_file_commit(const char *tempPath, const char *path, bool written)
{
	if (!written) {
		remove(tempPath);
		return false;
	}
	remove(path);
	return rename(tempPath, path) == 0;
}

bool safe_file_commit_or_delete(const char *tempPath, const char *path, bool written)
{
	if (safe_file_commit(tempPath, path, written)) {
		return true;
	}
	remove(tempPath);
	return false;
}

bool safe_file_write(const char *path, const void *head, size_t headSize, const void *body, size_t bodySize)
{
	char tempPath[SAFE_FILE_PATH_SIZE];
	safe_file_temp_path(path, tempPath);
	FILE *file = fopen(tempPath, "wb");
	if (!file) {
		return false;
	}
	bool written = fwrite(head, 1, headSize, file) == headSize &&
	               (!bodySize || fwrite(body, 1, bodySize, file) == bodySize);
	written = fclose(file) == 0 && written; // the file is closed even when writing failed
	return safe_file_commit_or_delete(tempPath, path, written);
}

bool safe_file_recover(const char *path)
{
	char tempPath[SAFE_FILE_PATH_SIZE];
	safe_file_temp_path(path, tempPath);
	return rename(tempPath, path) == 0;
}

FILE *safe_file_open_read(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file && safe_file_recover(path)) {
		file = fopen(path, "rb");
	}
	return file;
}
