// The brush library sheet and the brush settings sheet, and the brush previews they show.
#pragma once
#include <3ds.h>
#include <stdbool.h>

#define BRUSH_ICON_SIZE 22 // side of the square brush icons (brush_sheet_icon)

// BRUSH_ICON_SIZE x BRUSH_ICON_SIZE RGB, NULL while it is not made yet.
const u8 *brush_sheet_icon(int libraryIndex);
void brush_sheet_reset_preview_budget(void); // once a composed frame: one new brush preview may be made
void brush_sheet_library(int sheetLeft);
void brush_sheet_settings(int sheetLeft);
