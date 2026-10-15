// State several UI modules share: the tool and brush in hand, the layout settings, the stroke and eyedropper
// flags, the toast, the bottom-screen redraw request and the settings-changed flag.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "brush_engine.h"

enum {
	TOOL_BRUSH,
	TOOL_ERASER,
	TOOL_EYEDROPPER,
	TOOL_MOVE
};

#define STROKE_SMOOTHING_LEVELS 3 // off, low, high

extern int g_tool;
extern int g_previousTool; // the tool to go back to when the eyedropper is done
extern int g_brushRadius; // in canvas px
extern int g_strokeSmoothing;
extern int g_brushMode;
extern int g_smudgeStrength; // percent, for the smudge and smooth modes
extern Brush g_brush; // the brush in use: a copy of a preset, then edited
extern int g_presetIndex; // the library entry g_brush came from, -1 none
extern bool g_quitRequested;
extern bool g_unsaved;
extern bool g_stateDirty; // the current layer or a group's fold changed: only the exit save keeps it
extern bool g_leftHanded; // stylus in the left hand: the rail and the free hand's buttons swap sides
extern bool g_railHidden;
extern bool g_mascotHidden;
extern bool g_zoomButtonsShown; // the + / level / - buttons on the page (off: the pop-up palette zooms)
extern bool g_penDown; // a stroke is running
extern bool g_sampling; // the stylus is picking a colour
extern bool g_settingsDirty; // a setting differs from the saved settings

#define TOAST_PADDING 8 // around the toast's text

void app_toast(const char *format, ...); // a short message over the page
// A toast with a name in it: the name is cut, and ends in "...", so that the whole line fits beside the rail.
void app_toast_name(const char *before, const char *name, const char *after);
void app_toast_clear(void);
bool app_toast_shown(void);
const char *app_toast_text(void);
void app_request_bottom_redraw(void); // the bottom screen is composed again this frame and the next
bool app_bottom_redraw_pending(void);
void app_bottom_redraw_done(void); // one requested frame is composed
void app_tick(void); // once a frame: counts the toast down
void app_settings_changed(void); // the settings are saved once the input has paused
