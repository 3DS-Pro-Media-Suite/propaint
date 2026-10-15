// Switching tools. The brush and the eraser each keep their own preset, size and edits: g_brush, g_presetIndex
// and g_brushRadius hold the ones of the tool in hand, and switching between the two swaps them.
#pragma once
#include "brush_engine.h"

typedef struct {
	Brush brush;
	int presetIndex; // the library entry, -1 none
	int radius; // canvas px
} ToolBrush;

void tools_init(void); // after the brush library: the eraser starts with the first preset
void tool_select(int tool); // every tool change goes through here
int tool_brush_owner(void); // TOOL_BRUSH or TOOL_ERASER: whose brush is in hand
void tool_brush_get(int tool, ToolBrush *out); // the brush or the eraser's, the one in hand included
void tool_brush_set(int tool, const ToolBrush *values);
void tool_brushes_preset_deleted(int index); // library entry index was deleted
