#include <stdio.h>
#include <string.h>
#include "brush_library.h"
#include "app_state.h"
#include "brush_engine.h"
#include "krita_import.h"
#include "paths.h"
#include "rail.h"
#include "safe_file.h"
#include "tools.h"

#define LIBRARY_PATH APP_DIR "/brushes.lib"
#define LIBRARY_MAGIC 0x42494c42u // "BLIB"
// The Krita brushes a new library starts with, and the list of the ones that go on the pop-up palette.
#define ROMFS_BRUSH_DIR "romfs:/brushes"
#define ROMFS_FAVORITES_FILE ROMFS_BRUSH_DIR "/favorites.txt"
#define DEFAULT_FAVORITE_COUNT 10

// The library file: this header, then the brushes as they are in memory.
typedef struct {
	u32 magic;
	u32 recordSize; // sizeof(LibraryBrush): a file from a build with another layout is not read
	u32 count;
} LibraryFileHeader;

// The engine's built-in presets: a new or reset library starts from them. Fields left out are 0.
static const Brush PRESETS[] = {
        {.name = "Ink",
         .tip = TIP_ROUND,
         .hardness = 100,
         .ratio = 100,
         .spacing = 6,
         .flow = 100,
         .opacity = 100,
         .sizeSensors = SENSOR_PRESSURE,
         .sizeMin = 20,
         .opacityMin = 100,
         .sizeCurve = {0, 85, 150, 215, 255}},
        {.name = "Pencil",
         .tip = TIP_ROUND,
         .hardness = 85,
         .ratio = 100,
         .spacing = 10,
         .flow = 45,
         .opacity = 100,
         .textureStrength = 55,
         .sizeSensors = SENSOR_PRESSURE,
         .opacitySensors = SENSOR_PRESSURE,
         .sizeMin = 40,
         .opacityMin = 25,
         .opacityCurve = {0, 25, 90, 190, 255}},
        {.name = "Marker",
         .tip = TIP_ROUND,
         .hardness = 100,
         .ratio = 35,
         .angle = 35,
         .spacing = 8,
         .flow = 100,
         .opacity = 85,
         .sizeMin = 100,
         .opacityMin = 100},
        {.name = "Airbrush",
         .tip = TIP_ROUND,
         .hardness = 0,
         .ratio = 100,
         .spacing = 8,
         .flow = 10,
         .opacity = 100,
         .flowSensors = SENSOR_PRESSURE,
         .sizeMin = 100,
         .opacityMin = 100,
         .flowCurve = {0, 40, 110, 200, 255}},
        {.name = "Chalk",
         .tip = TIP_ROUND,
         .hardness = 75,
         .ratio = 100,
         .spacing = 14,
         .flow = 70,
         .opacity = 100,
         .scatter = 8,
         .jitter = 12,
         .textureStrength = 85,
         .sizeSensors = SENSOR_PRESSURE,
         .sizeMin = 50,
         .opacityMin = 100},
        {.name = "Flat",
         .tip = TIP_SQUARE,
         .hardness = 100,
         .ratio = 30,
         .follow = 1,
         .spacing = 5,
         .flow = 100,
         .opacity = 100,
         .sizeSensors = SENSOR_PRESSURE,
         .sizeMin = 50,
         .opacityMin = 100},
        {.name = "Taper",
         .tip = TIP_ROUND,
         .hardness = 100,
         .ratio = 100,
         .spacing = 5,
         .flow = 100,
         .opacity = 100,
         .sizeSensors = SENSOR_PRESSURE | SENSOR_FADE,
         .sizeMin = 8,
         .opacityMin = 100,
         .fade = 6},
};
#define PRESET_COUNT ((int)(sizeof PRESETS / sizeof PRESETS[0]))

LibraryBrush g_library[BRUSH_LIBRARY_MAX];
int g_libraryCount;

// The library file is written only when its bytes changed.
static bool g_libraryDirty;
static LibraryBrush g_librarySaved[BRUSH_LIBRARY_MAX]; // the library as its file holds it
static int g_librarySavedCount = -1;

void brush_library_reset(void)
{
	memset(g_library, 0, sizeof g_library);
	g_libraryCount = 0;
	for (int i = 0; i < PRESET_COUNT && g_libraryCount < BRUSH_LIBRARY_MAX; i++) {
		g_library[g_libraryCount].brush = PRESETS[i];
		g_libraryCount++;
	}
}

static size_t library_bytes(void)
{
	return (size_t)g_libraryCount * sizeof *g_library;
}

bool brush_library_save(void)
{
	LibraryFileHeader header = {LIBRARY_MAGIC, (u32)sizeof(LibraryBrush), (u32)g_libraryCount};
	return safe_file_write(LIBRARY_PATH, &header, sizeof header, g_library, library_bytes());
}

// Reads the library from its file. Returns false when the file does not hold a whole library of this build's
// LibraryBrush layout.
static bool read_library(FILE *file)
{
	LibraryFileHeader header;
	if (fread(&header, sizeof header, 1, file) != 1) {
		return false;
	}
	bool isLibrary = header.magic == LIBRARY_MAGIC && header.recordSize == sizeof(LibraryBrush);
	if (!isLibrary || header.count < 1 || header.count > BRUSH_LIBRARY_MAX) {
		return false;
	}
	if (fread(g_library, sizeof(LibraryBrush), header.count, file) != header.count) {
		return false;
	}
	g_libraryCount = (int)header.count;
	return true;
}

// When no brush is a favourite (no romfs list), the first ones go on the pop-up palette.
static void set_default_favorites(void)
{
	for (int i = 0; i < g_libraryCount; i++) {
		if (g_library[i].favorite) {
			return;
		}
	}
	for (int i = 0; i < g_libraryCount && i < DEFAULT_FAVORITE_COUNT; i++) {
		g_library[i].favorite = 1;
	}
}

// The engine's presets plus the Krita brushes in romfs.
static void seed_library(void)
{
	brush_library_reset();
	int fileCount = 0;
	krita_import_directory(ROMFS_BRUSH_DIR, ROMFS_FAVORITES_FILE, &fileCount);
	set_default_favorites();
	brush_library_save();
}

void brush_library_init(void)
{
	FILE *file = safe_file_open_read(LIBRARY_PATH);
	if (!file || !read_library(file)) {
		seed_library(); // no library yet, or one from a build with another Brush layout
	}
	if (file) {
		fclose(file);
	}
}

int brush_library_find(const char *name)
{
	for (int i = 0; i < g_libraryCount; i++) {
		if (!strncmp(g_library[i].brush.name, name, sizeof g_library[i].brush.name - 1)) {
			return i;
		}
	}
	return -1;
}

int brush_library_add(const LibraryBrush *brush)
{
	if (g_libraryCount >= BRUSH_LIBRARY_MAX) {
		return -1;
	}
	int index = g_libraryCount;
	g_library[index] = *brush;
	g_libraryCount++;
	return index;
}

void brush_library_delete(int index)
{
	// The library always keeps one brush.
	if (index < 0 || index >= g_libraryCount || g_libraryCount <= 1) {
		return;
	}
	int brushesAfter = g_libraryCount - index - 1;
	memmove(&g_library[index], &g_library[index + 1], (size_t)brushesAfter * sizeof(LibraryBrush));
	g_libraryCount--;
}

void brush_library_changed(void)
{
	g_libraryDirty = true;
}

bool brush_library_is_dirty(void)
{
	return g_libraryDirty;
}

void brush_library_mark_saved(void)
{
	memcpy(g_librarySaved, g_library, sizeof g_librarySaved);
	g_librarySavedCount = g_libraryCount;
}

void brush_library_flush(void)
{
	g_libraryDirty = false;
	if (g_libraryCount == g_librarySavedCount && memcmp(g_library, g_librarySaved, library_bytes()) == 0) {
		return;
	}
	if (brush_library_save()) {
		memcpy(g_librarySaved, g_library, library_bytes());
		g_librarySavedCount = g_libraryCount;
	}
}

bool brush_library_is_edited(void)
{
	return g_presetIndex < 0 || memcmp(&g_brush, &g_library[g_presetIndex].brush, sizeof g_brush) != 0;
}

void brush_library_select(int index)
{
	if (index < 0 || index >= g_libraryCount) {
		return;
	}
	const LibraryBrush *entry = &g_library[index];
	g_presetIndex = index;
	g_brush = entry->brush;
	if (entry->size) {
		rail_set_brush_size(entry->size);
	}
	int owner = tool_brush_owner();
	if (owner == TOOL_BRUSH) {
		g_brushMode = entry->mode;
	}
	tool_select(owner);
	app_settings_changed();
}
