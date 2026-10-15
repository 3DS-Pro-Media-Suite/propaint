#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "textures.h"
#include "paths.h"
#include "png_reader.h"
#include "safe_file.h"

#define ROMFS_TEXTURE_DIR "romfs:/textures"
#define TEXTURE_SIDE_MAX 512 // px: Krita's patterns are this size at most
#define CACHED_TEXTURES 3 // the stroke's, and the previews' around it
#define PNG_EXTENSION ".png"

typedef struct {
	char name[BRUSH_TEXTURE_NAME_SIZE];
	u8 source; // TextureSource
	bool unreadable; // a read failed: it is not tried again until the next scan
} TextureEntry;

typedef struct {
	char name[BRUSH_TEXTURE_NAME_SIZE]; // "" while the slot is free
	TextureTile tile;
	u8 *pixels;
	u32 lastUse;
} CachedTexture;

static TextureEntry g_textures[TEXTURES_MAX];
static int g_textureCount;
static CachedTexture g_cache[CACHED_TEXTURES];
static u32 g_useCount;

// A PNG file name that fits a brush, and that is a plain name rather than a path.
static bool usable_name(const char *name)
{
	size_t length = strlen(name);
	size_t extensionLength = strlen(PNG_EXTENSION);
	bool fits = length > extensionLength && length < BRUSH_TEXTURE_NAME_SIZE;
	return fits && !strcasecmp(name + length - extensionLength, PNG_EXTENSION) && !strpbrk(name, "/\\:");
}

static void add_entry(const char *name, TextureSource source)
{
	TextureEntry *entry = &g_textures[g_textureCount];
	snprintf(entry->name, sizeof entry->name, "%.*s", (int)sizeof entry->name - 1, name);
	entry->source = (u8)source;
	entry->unreadable = false;
	g_textureCount++;
}

static void scan_folder(const char *folder, TextureSource source)
{
	DIR *directory = opendir(folder);
	if (!directory) {
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(directory)) && g_textureCount < TEXTURES_MAX) {
		// A name in romfs wins over the same name on the SD card.
		if (usable_name(entry->d_name) && textures_find(entry->d_name) < 0) {
			add_entry(entry->d_name, source);
		}
	}
	closedir(directory);
}

// Pro Paint's textures first, then the SD card's, each by name.
static int compare_entries(const void *a, const void *b)
{
	const TextureEntry *first = a;
	const TextureEntry *second = b;
	if (first->source != second->source) {
		return first->source - second->source;
	}
	return strcasecmp(first->name, second->name);
}

void textures_scan(void)
{
	g_textureCount = 0;
	scan_folder(ROMFS_TEXTURE_DIR, TEXTURE_FROM_APP);
	scan_folder(TEXTURES_DIR, TEXTURE_FROM_SD_CARD);
	qsort(g_textures, (size_t)g_textureCount, sizeof g_textures[0], compare_entries);
}

int textures_count(void)
{
	return g_textureCount;
}

const char *textures_name(int index)
{
	return g_textures[index].name;
}

TextureSource textures_source(int index)
{
	return (TextureSource)g_textures[index].source;
}

int textures_find(const char *name)
{
	for (int i = 0; i < g_textureCount; i++) {
		if (!strcmp(g_textures[i].name, name)) {
			return i;
		}
	}
	return -1;
}

static void texture_path(int index, char path[SAFE_FILE_PATH_SIZE])
{
	const char *folder = g_textures[index].source == TEXTURE_FROM_APP ? ROMFS_TEXTURE_DIR : TEXTURES_DIR;
	snprintf(path, SAFE_FILE_PATH_SIZE, "%s/%s", folder, g_textures[index].name);
}

static CachedTexture *cached(const char *name)
{
	for (int i = 0; i < CACHED_TEXTURES; i++) {
		if (!strcmp(g_cache[i].name, name)) {
			return &g_cache[i];
		}
	}
	return NULL;
}

// A free slot, else the one used longest ago.
static CachedTexture *slot_to_fill(void)
{
	CachedTexture *oldest = &g_cache[0];
	for (int i = 0; i < CACHED_TEXTURES; i++) {
		if (!g_cache[i].name[0]) {
			return &g_cache[i];
		}
		if (g_cache[i].lastUse < oldest->lastUse) {
			oldest = &g_cache[i];
		}
	}
	return oldest;
}

const TextureTile *textures_tile(const char *name)
{
	if (!name[0]) {
		return NULL;
	}
	g_useCount++;
	CachedTexture *texture = cached(name);
	if (texture) {
		texture->lastUse = g_useCount;
		return &texture->tile;
	}
	int index = textures_find(name);
	if (index < 0 || g_textures[index].unreadable) {
		return NULL;
	}
	char path[SAFE_FILE_PATH_SIZE];
	texture_path(index, path);
	GreyImage image;
	if (!png_read_grey_file(path, TEXTURE_SIDE_MAX, &image)) {
		g_textures[index].unreadable = true;
		return NULL;
	}
	texture = slot_to_fill();
	free(texture->pixels);
	snprintf(texture->name, sizeof texture->name, "%s", name);
	texture->pixels = image.pixels;
	texture->tile.pixels = image.pixels;
	texture->tile.width = image.width;
	texture->tile.height = image.height;
	texture->lastUse = g_useCount;
	return &texture->tile;
}

bool textures_add_png(const char *name, const u8 *png, size_t size)
{
	if (textures_find(name) >= 0) {
		return true;
	}
	if (!usable_name(name) || g_textureCount >= TEXTURES_MAX) {
		return false;
	}
	char path[SAFE_FILE_PATH_SIZE];
	snprintf(path, sizeof path, "%s/%s", TEXTURES_DIR, name);
	if (!safe_file_write(path, png, size, NULL, 0)) {
		return false;
	}
	add_entry(name, TEXTURE_FROM_SD_CARD);
	return true;
}
