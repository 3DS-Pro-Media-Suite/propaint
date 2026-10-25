#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "app_state.h"
#include "brush_engine.h"
#include "brush_library.h"
#include "button_map.h"
#include "color.h"
#include "paths.h"
#include "pressure.h"
#include "safe_file.h"
#include "tools.h"

// Settings: a header with the record's size, then fields that are only ever appended. A file from an older
// build fills the fields it has; the ones it lacks keep their defaults, so a new build never resets the
// settings. The brush and the eraser in use are a separate file, kept only while the engine's Brush layout is
// unchanged.
#define SETTINGS_PATH APP_DIR "/propaint.cfg"
#define SETTINGS_MAGIC 0x54535050u // "PPST" in the file
#define SETTINGS_VERSION 1
#define SETTINGS_HEADER_SIZE 8 // SETTINGS_MAGIC, size and version
#define NO_PRESET_SAVED 255 // the saved preset index when g_presetIndex is -1
#define BRUSH_PATH APP_DIR "/brush.bin"
#define BRUSH_FILE_MAGIC 0x42525348u // "HSRB" in the file

typedef struct {
	u32 magic;
	u16 size;
	u16 version;
	u8 color[3];
	u8 brushRadius;
	u8 strokeSmoothing;
	u8 leftHanded;
	u8 railHidden;
	u8 recentColorCount;
	u8 recentColors[RECENT_COLORS_MAX][3];
	u8 brushMode;
	u8 smudgeStrength;
	u8 pressureWanted;
	u8 mascotHidden;
	u8 presetIndex;
	u8 buttonActions[BUTTON_COUNT];
	u8 zoomButtonsShown;
	u8 eraserPresetIndex;
	u8 eraserRadius;
	// append new fields here
} SettingsRecord;

// The brush file: this header, then the brush's Brush and the eraser's.
typedef struct {
	u32 magic;
	u32 brushSize; // sizeof(Brush) of the build that wrote the file
} BrushFileHeader;

enum {
	SAVED_BRUSH,
	SAVED_ERASER,
	SAVED_BRUSH_COUNT
};

static const int SAVED_BRUSH_TOOLS[SAVED_BRUSH_COUNT] = {TOOL_BRUSH, TOOL_ERASER};

static SettingsRecord g_settingsSaved; // the settings and the brushes as their files hold them
static Brush g_brushesSaved[SAVED_BRUSH_COUNT];

static u8 saved_preset_index(int presetIndex)
{
	return (u8)(presetIndex < 0 ? NO_PRESET_SAVED : presetIndex);
}

static int loaded_preset_index(u8 savedIndex)
{
	return savedIndex < g_libraryCount ? savedIndex : -1;
}

static void read_brushes(Brush brushes[SAVED_BRUSH_COUNT])
{
	for (int i = 0; i < SAVED_BRUSH_COUNT; i++) {
		ToolBrush toolBrush;
		tool_brush_get(SAVED_BRUSH_TOOLS[i], &toolBrush);
		brushes[i] = toolBrush.brush;
	}
}

static void fill_record(SettingsRecord *record)
{
	ToolBrush brush;
	ToolBrush eraser;
	tool_brush_get(TOOL_BRUSH, &brush);
	tool_brush_get(TOOL_ERASER, &eraser);
	memset(record, 0, sizeof *record);
	record->magic = SETTINGS_MAGIC;
	record->size = sizeof *record;
	record->version = SETTINGS_VERSION;
	memcpy(record->color, g_color, sizeof record->color);
	record->brushRadius = (u8)brush.radius;
	record->strokeSmoothing = (u8)g_strokeSmoothing;
	record->leftHanded = g_leftHanded;
	record->railHidden = g_railHidden;
	record->recentColorCount = (u8)color_recent_count();
	color_recent_copy(record->recentColors);
	record->brushMode = (u8)g_brushMode;
	record->smudgeStrength = (u8)g_smudgeStrength;
	record->pressureWanted = pressure_wanted();
	record->mascotHidden = g_mascotHidden;
	record->presetIndex = saved_preset_index(brush.presetIndex);
	memcpy(record->buttonActions, button_map_actions(), sizeof record->buttonActions);
	record->zoomButtonsShown = g_zoomButtonsShown;
	record->eraserPresetIndex = saved_preset_index(eraser.presetIndex);
	record->eraserRadius = (u8)eraser.radius;
}

void settings_mark_saved(void)
{
	fill_record(&g_settingsSaved);
	read_brushes(g_brushesSaved);
}

bool settings_is_dirty(void)
{
	return app_settings_pending() || brush_library_is_dirty();
}

// Writes only a file whose bytes changed.
static void write_changed_files(void)
{
	SettingsRecord record;
	fill_record(&record);
	if (memcmp(&record, &g_settingsSaved, sizeof record) &&
	    safe_file_write(SETTINGS_PATH, &record, sizeof record, NULL, 0)) {
		memcpy(&g_settingsSaved, &record, sizeof record);
	}
	BrushFileHeader header = {BRUSH_FILE_MAGIC, (u32)sizeof(Brush)};
	Brush brushes[SAVED_BRUSH_COUNT];
	read_brushes(brushes);
	if (memcmp(brushes, g_brushesSaved, sizeof brushes) &&
	    safe_file_write(BRUSH_PATH, &header, sizeof header, brushes, sizeof brushes)) {
		memcpy(g_brushesSaved, brushes, sizeof brushes);
	}
	app_settings_saved();
}

// Reads as much of the record as the file holds.
static void read_record(FILE *file, SettingsRecord *record)
{
	u32 magic = 0;
	u16 size = 0;
	if (fread(&magic, sizeof magic, 1, file) != 1 || magic != SETTINGS_MAGIC) {
		return;
	}
	if (fread(&size, sizeof size, 1, file) != 1 || size <= SETTINGS_HEADER_SIZE) {
		return;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		return;
	}
	size_t readSize = size < sizeof *record ? size : sizeof *record;
	u8 bytes[sizeof *record];
	if (fread(bytes, 1, readSize, file) == readSize) {
		memcpy(record, bytes, readSize);
	}
}

static bool radius_valid(int radius)
{
	return radius >= BRUSH_RADIUS_MIN && radius <= BRUSH_RADIUS_MAX;
}

// The brush or the eraser's preset and size. Its Brush comes from the brush file or the preset.
static void apply_tool_brush(int tool, u8 savedPresetIndex, u8 savedRadius)
{
	ToolBrush toolBrush;
	tool_brush_get(tool, &toolBrush);
	toolBrush.presetIndex = loaded_preset_index(savedPresetIndex);
	if (toolBrush.presetIndex >= 0) {
		toolBrush.brush = g_library[toolBrush.presetIndex].brush;
	}
	if (radius_valid(savedRadius)) {
		toolBrush.radius = savedRadius;
	}
	tool_brush_set(tool, &toolBrush);
}

// Values out of range keep the current setting.
static void apply_record(const SettingsRecord *record)
{
	apply_tool_brush(TOOL_BRUSH, record->presetIndex, record->brushRadius);
	apply_tool_brush(TOOL_ERASER, record->eraserPresetIndex, record->eraserRadius);
	if (record->strokeSmoothing < STROKE_SMOOTHING_LEVELS) {
		g_strokeSmoothing = record->strokeSmoothing;
	}
	g_leftHanded = record->leftHanded;
	g_railHidden = record->railHidden;
	if (record->recentColorCount <= RECENT_COLORS_MAX) {
		color_recent_restore(record->recentColors, record->recentColorCount);
	}
	color_set(record->color);
	if (record->brushMode < BRUSH_MODE_COUNT) {
		g_brushMode = record->brushMode;
	}
	if (record->smudgeStrength <= 100) {
		g_smudgeStrength = record->smudgeStrength;
	}
	pressure_restore_wanted(record->pressureWanted);
	g_mascotHidden = record->mascotHidden;
	g_zoomButtonsShown = record->zoomButtonsShown;
	bool actionsValid = true;
	for (int i = 0; i < BUTTON_COUNT; i++) {
		actionsValid &= record->buttonActions[i] < ACTION_COUNT;
	}
	if (actionsValid) {
		button_map_restore(record->buttonActions);
	}
}

// The brush and the eraser in use, with their edits. A file from another Brush layout keeps the presets'
// brushes apply_record took.
static void load_brushes(void)
{
	FILE *file = safe_file_open_read(BRUSH_PATH);
	if (!file) {
		return;
	}
	BrushFileHeader header;
	bool headerOk = fread(&header, sizeof header, 1, file) == 1 && header.magic == BRUSH_FILE_MAGIC &&
	                header.brushSize == sizeof(Brush);
	for (int i = 0; headerOk && i < SAVED_BRUSH_COUNT; i++) {
		Brush brush;
		if (fread(&brush, sizeof brush, 1, file) != 1 || !brush.spacing) {
			break;
		}
		ToolBrush toolBrush;
		tool_brush_get(SAVED_BRUSH_TOOLS[i], &toolBrush);
		toolBrush.brush = brush;
		tool_brush_set(SAVED_BRUSH_TOOLS[i], &toolBrush);
	}
	fclose(file);
}

void settings_load(void)
{
	SettingsRecord record;
	fill_record(&record); // the defaults for whatever the file lacks
	FILE *file = safe_file_open_read(SETTINGS_PATH);
	if (!file) {
		return;
	}
	read_record(file, &record);
	fclose(file);
	apply_record(&record);
	load_brushes();
}

void settings_save(void)
{
	write_changed_files();
	brush_library_flush();
}
