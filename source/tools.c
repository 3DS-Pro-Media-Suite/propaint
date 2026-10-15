#include "tools.h"
#include "app_state.h"
#include "brush_library.h"

#define ERASER_RADIUS_DEFAULT 12 // canvas px

static int g_handOwner = TOOL_BRUSH; // whose brush g_brush, g_presetIndex and g_brushRadius hold
static ToolBrush g_putAway; // the other one's

static bool draws(int tool)
{
	return tool == TOOL_BRUSH || tool == TOOL_ERASER;
}

static void read_hand(ToolBrush *out)
{
	out->brush = g_brush;
	out->presetIndex = g_presetIndex;
	out->radius = g_brushRadius;
}

static void fill_hand(const ToolBrush *values)
{
	g_brush = values->brush;
	g_presetIndex = values->presetIndex < g_libraryCount ? values->presetIndex : -1;
	g_brushRadius = values->radius;
	app_request_bottom_redraw();
}

void tools_init(void)
{
	g_putAway.brush = g_library[0].brush;
	g_putAway.presetIndex = 0;
	g_putAway.radius = ERASER_RADIUS_DEFAULT;
}

void tool_select(int tool)
{
	// The eyedropper goes back to the tool before it.
	if (tool == TOOL_EYEDROPPER && g_tool != TOOL_EYEDROPPER) {
		g_previousTool = draws(g_tool) ? g_tool : g_handOwner;
	}
	if (draws(tool) && tool != g_handOwner) {
		ToolBrush hand;
		read_hand(&hand);
		fill_hand(&g_putAway);
		g_putAway = hand;
		g_handOwner = tool;
	}
	g_tool = tool;
}

int tool_brush_owner(void)
{
	return g_handOwner;
}

void tool_brush_get(int tool, ToolBrush *out)
{
	if (tool == g_handOwner) {
		read_hand(out);
	} else {
		*out = g_putAway;
	}
}

void tool_brush_set(int tool, const ToolBrush *values)
{
	if (tool == g_handOwner) {
		fill_hand(values);
	} else {
		g_putAway = *values;
	}
}

void tool_brushes_preset_deleted(int index)
{
	if (g_putAway.presetIndex == index) {
		g_putAway.presetIndex = -1;
	} else if (g_putAway.presetIndex > index) {
		g_putAway.presetIndex--;
	}
}
