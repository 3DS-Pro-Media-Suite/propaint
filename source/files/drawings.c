// The drawings on the SD card: list, open, new, rename, duplicate, delete, thumbnails.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "drawings.h"
#include "ascii.h"
#include "color_units.h"
#include "document.h"
#include "paths.h"
#include "render.h"
#include "safe_file.h"

#define THUMBNAILS_DIR APP_DIR "/thumbs"
#define LAST_OPEN_PATH APP_DIR "/last.txt"
#define DRAWING_SUFFIX ".propaint"
#define THUMBNAIL_SUFFIX ".thm"
#define INTERRUPTED_SAVE_SUFFIX DRAWING_SUFFIX ".tmp"
#define PATH_SIZE 96
#define RECOVERED_SAVES_MAX 8
#define COPY_BUFFER_SIZE 16384

// Names
#define DEFAULT_DRAWING_NAME "Drawing 1"
#define FAT_RESERVED_CHARS "\\/:*?\"<>|"
#define FIRST_DUPLICATE_NUMBER 2
#define DUPLICATE_NUMBER_LIMIT 10000
#define COPY_SUFFIX " copy"
// Name characters kept before " <number>" and before COPY_SUFFIX. They decide the names of copies, so
// they stay fixed.
#define NUMBERED_STEM_MAX (DRAWING_NAME_MAX - 6)
#define COPY_STEM_MAX (DRAWING_NAME_MAX - 7)
#define NUMBERED_NAME_SIZE (NUMBERED_STEM_MAX + (int)sizeof " -2147483648") // the stem, a space and any int

// Thumbnail file: a header (THUMBNAIL_MAGIC, width, height, two spare bytes), then the RGB pixels.
#define THUMBNAIL_MAGIC "THM1"
#define THUMBNAIL_MAGIC_SIZE 4
#define THUMBNAIL_WIDTH_OFFSET 4 // the header byte that holds the width
#define THUMBNAIL_HEIGHT_OFFSET 5
#define THUMBNAIL_HEADER_SIZE 8
#define THUMBNAIL_PIXEL_BYTES (DRAWING_THUMBNAIL_WIDTH * DRAWING_THUMBNAIL_HEIGHT * RGB_PIXEL_BYTES)

DrawingFile g_drawings[DRAWINGS_MAX];
int g_drawingCount;
char g_drawingName[DRAWING_NAME_MAX + 1] = DEFAULT_DRAWING_NAME;

// Its .propaint path, in a static buffer that the next call overwrites.
static const char *drawing_path(const char *name)
{
	static char path[PATH_SIZE];
	snprintf(path, sizeof path, DRAWINGS_DIR "/%.*s" DRAWING_SUFFIX, DRAWING_NAME_MAX, name);
	return path;
}

// Its thumbnail path, in a static buffer that the next call overwrites.
static const char *thumbnail_path(const char *name)
{
	static char path[PATH_SIZE];
	snprintf(path, sizeof path, THUMBNAILS_DIR "/%.*s" THUMBNAIL_SUFFIX, DRAWING_NAME_MAX, name);
	return path;
}

bool drawings_exists(const char *name)
{
	struct stat info;
	return stat(drawing_path(name), &info) == 0;
}

// The drawing to open at the next start.
static void remember_open_drawing(void)
{
	FILE *file = fopen(LAST_OPEN_PATH, "wb");
	if (file) {
		fputs(g_drawingName, file);
		fclose(file);
	}
}

// The drawing name in a file name that ends in suffix. False for another suffix or a name that is too long.
static bool name_before_suffix(const char *fileName, const char *suffix, char name[DRAWING_NAME_MAX + 1])
{
	size_t fileNameLength = strlen(fileName);
	size_t suffixLength = strlen(suffix);
	if (fileNameLength <= suffixLength) {
		return false;
	}
	size_t nameLength = fileNameLength - suffixLength;
	if (nameLength > DRAWING_NAME_MAX || strcasecmp(fileName + nameLength, suffix)) {
		return false;
	}
	memcpy(name, fileName, nameLength);
	name[nameLength] = 0;
	return true;
}

// A save cut off between removing the old file and renaming the new one leaves only <name>.propaint.tmp: it
// gets its name back. A save cut off while it is written will not open, and no older file is lost.
static void recover_interrupted_saves(void)
{
	char names[RECOVERED_SAVES_MAX][DRAWING_NAME_MAX + 1];
	int count = 0;
	DIR *folder = opendir(DRAWINGS_DIR);
	if (!folder) {
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(folder)) && count < RECOVERED_SAVES_MAX) {
		if (name_before_suffix(entry->d_name, INTERRUPTED_SAVE_SUFFIX, names[count])) {
			count++;
		}
	}
	closedir(folder);
	for (int i = 0; i < count; i++) {
		if (!drawings_exists(names[i])) {
			safe_file_recover(drawing_path(names[i]));
		}
	}
}

void drawings_init(void)
{
	mkdir(DRAWINGS_DIR, FOLDER_MODE);
	mkdir(EXPORTS_DIR, FOLDER_MODE);
	mkdir(THUMBNAILS_DIR, FOLDER_MODE);
	recover_interrupted_saves();
	char lastName[DRAWING_NAME_MAX + 1] = "";
	FILE *file = fopen(LAST_OPEN_PATH, "rb");
	if (file) {
		size_t length = fread(lastName, 1, DRAWING_NAME_MAX, file);
		lastName[length] = 0;
		fclose(file);
	}
	if (lastName[0] && drawings_exists(lastName)) {
		strcpy(g_drawingName, lastName);
		return;
	}
	drawings_scan();
	if (g_drawingCount) {
		strcpy(g_drawingName, g_drawings[0].name);
	} else {
		drawings_unique_name(DEFAULT_DRAWING_NAME, g_drawingName, sizeof g_drawingName);
	}
}

bool drawings_clean_name(const char *text, char *out, size_t outSize)
{
	size_t length = 0;
	while (*text == ' ' || *text == '.') {
		text++;
	}
	for (; *text && length + 1 < outSize && length < DRAWING_NAME_MAX; text++) {
		if (!ascii_is_printable(*text) || strchr(FAT_RESERVED_CHARS, *text)) {
			continue;
		}
		out[length] = *text;
		length++;
	}
	while (length && (out[length - 1] == ' ' || out[length - 1] == '.')) {
		length--;
	}
	out[length] = 0;
	return length > 0;
}

static bool is_number(const char *text)
{
	return text[0] && strspn(text, "0123456789") == strlen(text);
}

void drawings_unique_name(const char *base, char *out, size_t outSize)
{
	char stem[DRAWING_NAME_MAX + 1];
	snprintf(stem, sizeof stem, "%s", base);
	// "Drawing 3" continues from 3, anything else gets " 2", " 3", ...
	int number = FIRST_DUPLICATE_NUMBER;
	char *lastSpace = strrchr(stem, ' ');
	if (lastSpace && is_number(lastSpace + 1)) {
		number = atoi(lastSpace + 1) + 1;
		*lastSpace = 0;
	}
	snprintf(out, outSize, "%s", base);
	while (drawings_exists(out) && number < DUPLICATE_NUMBER_LIMIT) {
		char candidate[NUMBERED_NAME_SIZE];
		snprintf(candidate, sizeof candidate, "%.*s %d", NUMBERED_STEM_MAX, stem, number);
		number++;
		snprintf(out, outSize, "%s", candidate);
	}
}

static void free_drawing_list(void)
{
	for (int i = 0; i < g_drawingCount; i++) {
		free(g_drawings[i].thumbnail);
	}
	g_drawingCount = 0;
}

static bool thumbnail_header_matches(const u8 *header)
{
	return !memcmp(header, THUMBNAIL_MAGIC, THUMBNAIL_MAGIC_SIZE) &&
	       header[THUMBNAIL_WIDTH_OFFSET] == DRAWING_THUMBNAIL_WIDTH &&
	       header[THUMBNAIL_HEIGHT_OFFSET] == DRAWING_THUMBNAIL_HEIGHT;
}

// NULL when there is no thumbnail of the current size.
static u8 *read_thumbnail(const char *name)
{
	FILE *file = fopen(thumbnail_path(name), "rb");
	if (!file) {
		return NULL;
	}
	u8 header[THUMBNAIL_HEADER_SIZE];
	u8 *pixels = malloc(THUMBNAIL_PIXEL_BYTES);
	bool ok = pixels && fread(header, 1, THUMBNAIL_HEADER_SIZE, file) == THUMBNAIL_HEADER_SIZE &&
	          thumbnail_header_matches(header) &&
	          fread(pixels, 1, THUMBNAIL_PIXEL_BYTES, file) == THUMBNAIL_PIXEL_BYTES;
	fclose(file);
	if (!ok) {
		free(pixels);
		return NULL;
	}
	return pixels;
}

static bool write_thumbnail(const char *name)
{
	u8 *pixels = render_flattened(DRAWING_THUMBNAIL_WIDTH, DRAWING_THUMBNAIL_HEIGHT);
	if (!pixels) {
		return false;
	}
	FILE *file = fopen(thumbnail_path(name), "wb");
	bool ok = file != NULL;
	if (file) {
		u8 header[THUMBNAIL_HEADER_SIZE] = {0};
		memcpy(header, THUMBNAIL_MAGIC, THUMBNAIL_MAGIC_SIZE);
		header[THUMBNAIL_WIDTH_OFFSET] = DRAWING_THUMBNAIL_WIDTH;
		header[THUMBNAIL_HEIGHT_OFFSET] = DRAWING_THUMBNAIL_HEIGHT;
		ok = fwrite(header, 1, THUMBNAIL_HEADER_SIZE, file) == THUMBNAIL_HEADER_SIZE &&
		     fwrite(pixels, 1, THUMBNAIL_PIXEL_BYTES, file) == THUMBNAIL_PIXEL_BYTES;
		ok = fclose(file) == 0 && ok;
	}
	free(pixels);
	return ok;
}

static int newest_first(const void *left, const void *right)
{
	const DrawingFile *a = left;
	const DrawingFile *b = right;
	if (a->modifiedTime < b->modifiedTime) {
		return 1;
	}
	if (a->modifiedTime > b->modifiedTime) {
		return -1;
	}
	return strcasecmp(a->name, b->name);
}

// stat() leaves st_mtime at 0 on the SD card.
static time_t file_modified_time(const char *path)
{
	u64 modified = 0;
	return R_SUCCEEDED(archive_getmtime(path, &modified)) ? (time_t)modified : 0;
}

// Where a drawing changed at modifiedTime goes in the list: the next free entry, or in a full list the
// oldest entry when this drawing is newer. -1 when it is left out.
static int list_slot(time_t modifiedTime)
{
	if (g_drawingCount < DRAWINGS_MAX) {
		int slot = g_drawingCount;
		g_drawingCount++;
		return slot;
	}
	int oldest = 0;
	for (int i = 1; i < DRAWINGS_MAX; i++) {
		if (g_drawings[i].modifiedTime < g_drawings[oldest].modifiedTime) {
			oldest = i;
		}
	}
	if (g_drawings[oldest].modifiedTime >= modifiedTime) {
		return -1;
	}
	return oldest;
}

// With more than DRAWINGS_MAX drawings, the newest are listed.
void drawings_scan(void)
{
	free_drawing_list();
	DIR *folder = opendir(DRAWINGS_DIR);
	if (!folder) {
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(folder))) {
		char name[DRAWING_NAME_MAX + 1];
		if (!name_before_suffix(entry->d_name, DRAWING_SUFFIX, name)) {
			continue;
		}
		struct stat info;
		if (stat(drawing_path(name), &info) != 0) {
			continue;
		}
		time_t modifiedTime = file_modified_time(drawing_path(name));
		int slot = list_slot(modifiedTime);
		if (slot < 0) {
			continue;
		}
		DrawingFile *drawing = &g_drawings[slot];
		memset(drawing, 0, sizeof *drawing);
		strcpy(drawing->name, name);
		drawing->modifiedTime = modifiedTime;
	}
	closedir(folder);
	for (int i = 0; i < g_drawingCount; i++) {
		g_drawings[i].thumbnail = read_thumbnail(g_drawings[i].name);
	}
	qsort(g_drawings, g_drawingCount, sizeof *g_drawings, newest_first);
}

int drawings_find(const char *name)
{
	for (int i = 0; i < g_drawingCount; i++) {
		if (!strcmp(g_drawings[i].name, name)) {
			return i;
		}
	}
	return -1;
}

bool drawings_save_open(void)
{
	if (!document_save(drawing_path(g_drawingName))) {
		return false;
	}
	write_thumbnail(g_drawingName);
	remember_open_drawing();
	return true;
}

bool drawings_open(const char *name)
{
	if (!document_load(drawing_path(name))) {
		return false;
	}
	snprintf(g_drawingName, sizeof g_drawingName, "%s", name);
	remember_open_drawing();
	return true;
}

bool drawings_new(void)
{
	char name[DRAWING_NAME_MAX + 1];
	drawings_unique_name(DEFAULT_DRAWING_NAME, name, sizeof name);
	document_reset();
	snprintf(g_drawingName, sizeof g_drawingName, "%s", name);
	return drawings_save_open();
}

bool drawings_rename(const char *from, const char *to)
{
	if (!strcmp(from, to)) {
		return true;
	}
	if (drawings_exists(to)) {
		return false;
	}
	char sourcePath[PATH_SIZE];
	snprintf(sourcePath, sizeof sourcePath, "%s", drawing_path(from));
	if (rename(sourcePath, drawing_path(to)) != 0) {
		return false;
	}
	snprintf(sourcePath, sizeof sourcePath, "%s", thumbnail_path(from));
	rename(sourcePath, thumbnail_path(to));
	if (!strcmp(g_drawingName, from)) {
		snprintf(g_drawingName, sizeof g_drawingName, "%s", to);
		remember_open_drawing();
	}
	return true;
}

bool drawings_delete(const char *name)
{
	if (remove(drawing_path(name)) != 0) {
		return false;
	}
	remove(thumbnail_path(name));
	return true;
}

// A failed copy leaves no partial file behind.
static bool copy_file(const char *sourcePath, const char *destinationPath)
{
	FILE *source = fopen(sourcePath, "rb");
	if (!source) {
		return false;
	}
	FILE *destination = fopen(destinationPath, "wb");
	if (!destination) {
		fclose(source);
		return false;
	}
	static u8 buffer[COPY_BUFFER_SIZE];
	size_t bytesRead;
	bool ok = true;
	while (ok && (bytesRead = fread(buffer, 1, sizeof buffer, source)) > 0) {
		ok = fwrite(buffer, 1, bytesRead, destination) == bytesRead;
	}
	fclose(source);
	ok = fclose(destination) == 0 && ok;
	if (!ok) {
		remove(destinationPath);
	}
	return ok;
}

bool drawings_duplicate(const char *name, char *out, size_t outSize)
{
	char base[DRAWING_NAME_MAX + 1];
	snprintf(base, sizeof base, "%.*s" COPY_SUFFIX, COPY_STEM_MAX, name);
	drawings_unique_name(base, out, outSize);
	char sourcePath[PATH_SIZE];
	snprintf(sourcePath, sizeof sourcePath, "%s", drawing_path(name));
	if (!copy_file(sourcePath, drawing_path(out))) {
		return false;
	}
	snprintf(sourcePath, sizeof sourcePath, "%s", thumbnail_path(name));
	copy_file(sourcePath, thumbnail_path(out));
	return true;
}
