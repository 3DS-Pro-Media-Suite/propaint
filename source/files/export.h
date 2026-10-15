// Exporting the open drawing: a layered OpenRaster (.ora) or a flattened PNG. path receives the file written
// (inside EXPORTS_DIR).
#pragma once
#include <stdbool.h>
#include <stddef.h>

enum {
	EXPORT_ORA,
	EXPORT_PNG,
	EXPORT_FORMAT_COUNT
};

const char *export_file_extension(int format); // without the dot
bool export_ora(const char *name, char *path, size_t pathSize);
bool export_png(const char *name, char *path, size_t pathSize);
