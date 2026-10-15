// Krita preset import into the brush library: .kpp files and .bundle archives.
#pragma once
#include <stddef.h>

// Imports every .kpp and .bundle in folder. A .kpp whose file name is a line of the favourites file
// (favoritesPath, NULL for none) goes on the pop-up palette. fileCount gets the number of files found.
// Returns the number of brushes added, -1 when the folder cannot be opened.
int krita_import_directory(const char *folder, const char *favoritesPath, int *fileCount);
// Imports the user's BRUSH_IMPORT_DIR and writes a summary into message. Returns the number of brushes added.
int krita_import_folder(char *message, size_t messageSize);
