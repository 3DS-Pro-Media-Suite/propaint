// The drawings on the SD card (file manager back end). Every drawing is DRAWINGS_DIR/<name>.propaint; its
// thumbnail lives in APP_DIR/thumbs/<name>.thm and exports go to EXPORTS_DIR/<name>.ora|png (re-exporting
// overwrites the file).
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define DRAWING_THUMBNAIL_WIDTH 80 // thumbnail size (RGB, row-major)
#define DRAWING_THUMBNAIL_HEIGHT 60
#define DRAWINGS_MAX 64
#define DRAWING_NAME_MAX 40

typedef struct {
	char name[DRAWING_NAME_MAX + 1];
	time_t modifiedTime;
	u8 *thumbnail; // DRAWING_THUMBNAIL_WIDTH * DRAWING_THUMBNAIL_HEIGHT * 3, or NULL when there is none yet
} DrawingFile;

int drawings_count(void); // the drawings drawings_scan listed
const DrawingFile *drawings_at(int index); // newest first
extern char g_drawingName[DRAWING_NAME_MAX + 1]; // the drawing that is open

// Sets up the save guard, makes the folders and sets g_drawingName to the drawing opened last.
void drawings_init(void);
void drawings_scan(void);
int drawings_find(const char *name); // the index for drawings_at, -1 when missing
bool drawings_exists(const char *name);
// base, or "base 2", "base 3", ... that is not taken
void drawings_unique_name(const char *base, char *out, size_t outSize);
// Drops characters FAT cannot store. False when nothing is left.
bool drawings_clean_name(const char *text, char *out, size_t outSize);
bool drawings_save_open(void); // the open drawing and its thumbnail
bool drawings_open(const char *name); // the caller saves the current drawing first
bool drawings_new(void); // a fresh document under a new name, saved at once
bool drawings_rename(const char *from, const char *to);
bool drawings_delete(const char *name);
// Copies the drawing and its thumbnail under a new name, which goes to out.
bool drawings_duplicate(const char *name, char *out, size_t outSize);
