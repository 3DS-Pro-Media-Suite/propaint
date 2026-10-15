#include <stdarg.h>
#include <stdio.h>
#include "app_state.h"
#include "draw.h"
#include "framebuffer.h"
#include "rail.h"

#define TOAST_FRAMES 150 // 2.5 s at 60 fps
#define TOAST_TEXT_SIZE 80
#define TOAST_TEXT_WIDTH (BOTTOM_SCREEN_WIDTH - RAIL_WIDTH - 2 * TOAST_PADDING)
#define TOAST_CUT_MARK "..." // ends a name that was cut to fit
#define BOTTOM_REDRAW_FRAMES 2 // a change is composed this frame and once more
#define BRUSH_RADIUS_DEFAULT 6 // canvas px
#define STROKE_SMOOTHING_DEFAULT 1 // low
#define SMUDGE_STRENGTH_DEFAULT 50 // percent

int g_brushRadius = BRUSH_RADIUS_DEFAULT;
int g_strokeSmoothing = STROKE_SMOOTHING_DEFAULT;
int g_brushMode = BRUSH_MODE_PAINT;
int g_smudgeStrength = SMUDGE_STRENGTH_DEFAULT;
Brush g_brush;
int g_presetIndex;

int g_tool = TOOL_BRUSH;
int g_previousTool = TOOL_BRUSH;

bool g_quitRequested;
bool g_unsaved;
bool g_stateDirty;
bool g_leftHanded;
bool g_railHidden;
bool g_mascotHidden;
bool g_zoomButtonsShown;
bool g_sampling;
bool g_penDown;
bool g_settingsDirty;

static char g_toastText[TOAST_TEXT_SIZE];
static int g_toastFrames; // frames the toast stays
static int g_bottomRedrawFrames = BOTTOM_REDRAW_FRAMES; // bottom-screen frames that must still be composed

void app_toast(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(g_toastText, sizeof g_toastText, format, arguments);
	va_end(arguments);
	g_toastFrames = TOAST_FRAMES;
	g_bottomRedrawFrames = BOTTOM_REDRAW_FRAMES;
}

void app_toast_name(const char *before, const char *name, const char *after)
{
	int nameRoom =
	        TOAST_TEXT_WIDTH - draw_text_width(FONT_SMALL, before) - draw_text_width(FONT_SMALL, after);
	const char *cutMark = "";
	if (draw_text_width(FONT_SMALL, name) > nameRoom) {
		nameRoom -= draw_text_width(FONT_SMALL, TOAST_CUT_MARK);
		cutMark = TOAST_CUT_MARK;
	}
	char fittedName[TOAST_TEXT_SIZE];
	draw_fit_text(fittedName, sizeof fittedName, name, nameRoom);
	app_toast("%s%s%s%s", before, fittedName, cutMark, after);
}

void app_toast_clear(void)
{
	g_toastFrames = 0;
}

bool app_toast_shown(void)
{
	return g_toastFrames > 0;
}

const char *app_toast_text(void)
{
	return g_toastText;
}

void app_request_bottom_redraw(void)
{
	g_bottomRedrawFrames = BOTTOM_REDRAW_FRAMES;
}

bool app_bottom_redraw_pending(void)
{
	return g_bottomRedrawFrames > 0;
}

void app_bottom_redraw_done(void)
{
	if (g_bottomRedrawFrames > 0) {
		g_bottomRedrawFrames--;
	}
}

void app_tick(void)
{
	if (g_toastFrames > 0) {
		g_toastFrames--;
	}
}

void app_settings_changed(void)
{
	g_settingsDirty = true;
}
