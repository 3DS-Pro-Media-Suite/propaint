// Pro Paint: a layered drawing app for the 3DS with pixel layers and signed-distance-field ("shape") layers.
// This file starts the app, runs the main loop and shuts it down.
#include <3ds.h>
#include <stdio.h>
#include <sys/stat.h>
#include "app_state.h"
#include "brush_engine.h"
#include "brush_library.h"
#include "button_map.h"
#include "color.h"
#include "document.h"
#include "drawings.h"
#include "ftp.h"
#include "ftp_control.h"
#include "input.h"
#include "mascot.h"
#include "math_util.h"
#include "page_view.h"
#include "paths.h"
#include "pen.h"
#include "pressure.h"
#include "screens.h"
#include "session.h"
#include "settings.h"
#include "textures.h"
#include "tools.h"

#define SETTINGS_IDLE_MS 2000 // the settings are saved once the input has paused this long
#define AUTOSAVE_IDLE_MS 30000 // unsaved work is saved once the input has paused this long

// Only the framebuffers use linear memory. Left at 0, libctru keeps up to 32 MB of it, and an Old 3DS has
// about 28 MB left for the layers.
u32 __ctru_linear_heap_size = 8 * MEBIBYTE;

static u64 g_lastInputTime;
static u64 g_autosavedAfterInput; // the input time the last autosave followed
static aptHookCookie g_aptHookCookie;

static void create_directories(void)
{
	mkdir(HOMEBREW_DIR, FOLDER_MODE);
	mkdir(APP_DIR, FOLDER_MODE);
	mkdir(DRAWINGS_DIR, FOLDER_MODE);
	mkdir(BRUSH_IMPORT_DIR, FOLDER_MODE);
	mkdir(TEXTURES_DIR, FOLDER_MODE);
}

// HOME, an applet or sleep: the touch report goes back to the system and the settings are saved.
static void handle_apt_event(APT_HookType type, void *unused)
{
	(void)unused;
	if (type != APTHOOK_ONSUSPEND && type != APTHOOK_ONSLEEP) {
		return;
	}
	pressure_pause();
	settings_save();
}

static void open_last_drawing(void)
{
	if (drawings_open(g_drawingName)) {
		app_toast("Welcome back");
		g_unsaved = false;
		return;
	}
	if (drawings_exists(g_drawingName)) {
		// The file stays as it is on the card: the page gets a new name.
		app_toast_name("Could not open ", g_drawingName, "");
		char freeName[DRAWING_NAME_MAX + 1];
		drawings_unique_name(g_drawingName, freeName, sizeof freeName);
		snprintf(g_drawingName, sizeof g_drawingName, "%s", freeName);
		return;
	}
	app_toast("Draw with the stylus. START: menu");
}

static bool start_up(void)
{
	gfxInitDefault();
	osSetSpeedupEnable(true);
	irrstInit();
	create_directories();
	if (!page_view_init() || !brush_engine_init() || !document_reset()) {
		irrstExit();
		osSetSpeedupEnable(false);
		gfxExit();
		return false;
	}
	romfsInit();
	textures_scan(); // before the library: its Krita brushes look for their textures
	brush_library_init();
	brush_library_mark_saved();
	g_brush = g_library[0].brush;
	tools_init();
	button_map_defaults();
	color_init();
	settings_load();
	settings_mark_saved(); // nothing to write until a setting changes
	pressure_open();
	aptHook(&g_aptHookCookie, handle_apt_event, NULL);
	drawings_init();
	open_last_drawing();
	page_view_set_zoom_step(ZOOM_STEP_FIT);
	mascot_init();
	return true;
}

// A flat battery then loses only the last moments. One try per pause, so a failing card does not show its
// error again and again.
static void autosave(u64 now)
{
	bool paused = now - g_lastInputTime > AUTOSAVE_IDLE_MS;
	if (!g_unsaved || !paused || g_autosavedAfterInput == g_lastInputTime ||
	    session_pending_job() != SESSION_JOB_NONE) {
		return;
	}
	g_autosavedAfterInput = g_lastInputTime;
	session_start_job(SESSION_JOB_SAVE, NULL, "Saving...");
}

static void run_frame(void)
{
	hidScanInput();
	irrstScanInput();
	pressure_poll();
	u32 pressedKeys = hidKeysDown();
	u32 heldKeys = hidKeysHeld();
	u32 releasedKeys = hidKeysUp();
	u64 now = osGetTime();
	if (pressedKeys | heldKeys) {
		mascot_input();
		g_lastInputTime = now;
	}
	input_update(pressedKeys, heldKeys, releasedKeys);
	session_tick();
	mascot_tick();
	app_tick();
	page_view_render_pending();
	if (settings_is_dirty() && now - g_lastInputTime > SETTINGS_IDLE_MS) {
		settings_save();
	}
	autosave(now);
	ftp_control_watch(now);
	screens_draw(pressedKeys, releasedKeys);
}

// True when the user quit from the menu, false when the system closed the app.
static bool run_main_loop(void)
{
	while (aptMainLoop()) {
		if (g_quitRequested && session_ready_to_quit()) {
			return true;
		}
		run_frame();
	}
	return false;
}

static void shut_down(bool quitFromMenu)
{
	aptUnhook(&g_aptHookCookie);
	// Closed from HOME or by the power button: nothing can be asked, so save what there is.
	if (!quitFromMenu) {
		if (g_penDown) {
			pen_up();
		}
		if (g_unsaved || g_stateDirty) {
			session_save(true);
		}
	}
	settings_save();
	ftp_exit();
	pressure_close();
	romfsExit();
	irrstExit();
	page_view_exit();
	osSetSpeedupEnable(false);
	gfxExit();
}

int main(void)
{
	if (!start_up()) {
		return 1;
	}
	bool quitFromMenu = run_main_loop();
	shut_down(quitFromMenu);
	return 0;
}
