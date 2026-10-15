#include <stdio.h>
#include "session.h"
#include "app_state.h"
#include "drawings.h"
#include "drawings_sheet.h"
#include "export.h"
#include "layers_sheet.h"
#include "mascot.h"
#include "page_view.h"
#include "pen.h"
#include "ui.h"

#define JOB_DELAY_FRAMES 3 // the job's toast is on screen before the app gets busy
#define EXPORT_PATH_MAX 112
#define EXTENSION_TEXT_SIZE 10 // " as .ora" and its terminating zero

static bool g_quitAnyway; // the save at Save and exit failed once: the next one quits even if it fails again
static int g_pendingJob;
static int g_jobDelayFrames; // frames until the pending job runs
static char g_jobDrawingName[DRAWING_NAME_MAX + 1];

void session_save(bool quiet)
{
	if (drawings_save_open()) {
		g_unsaved = false;
		g_stateDirty = false;
		g_quitAnyway = false;
		if (!quiet) {
			app_toast("Saved");
			mascot_event(MASCOT_SAVED);
		}
	} else {
		app_toast("Save failed");
		mascot_event(MASCOT_FAILED);
	}
}

// Save and exit stays in the app when the save fails, so the work is not dropped without a word; the next one
// quits.
bool session_ready_to_quit(void)
{
	g_quitRequested = false;
	if (g_penDown) {
		pen_up();
	}
	if (!g_unsaved) {
		if (g_stateDirty) {
			drawings_save_open();
		}
		return true;
	}
	session_save(true);
	if (!g_unsaved || g_quitAnyway) {
		return true;
	}
	g_quitAnyway = true;
	app_toast("Not saved! Exit again to quit anyway");
	return false;
}

bool session_quit_failed(void)
{
	return g_quitAnyway;
}

void session_start_job(int job, const char *drawingName, const char *toast)
{
	g_pendingJob = job;
	g_jobDelayFrames = JOB_DELAY_FRAMES;
	snprintf(g_jobDrawingName, sizeof g_jobDrawingName, "%s", drawingName ? drawingName : "");
	if (toast) {
		app_toast("%s", toast);
	}
}

// The job exports in the format the Export page shows when it runs, not the one at the tap.
void session_start_export(void)
{
	session_start_job(SESSION_JOB_EXPORT, NULL, "Exporting...");
}

// Another document is in g_document now.
static void after_document_switch(void)
{
	g_unsaved = false;
	g_stateDirty = false;
	layers_sheet_reset_scroll();
	page_view_set_zoom_step(ZOOM_STEP_FIT);
	page_view_redraw_all();
}

static void run_list_job(void)
{
	if (g_unsaved) {
		app_toast_clear(); // the "Saving..." toast goes; a failure shows its own
		session_save(true);
	}
	drawings_scan();
}

// Skipped when the pen went down meanwhile: the next pause saves.
static void run_save_job(void)
{
	if (g_penDown) {
		return;
	}
	app_toast_clear(); // the "Saving..." toast goes; a failure shows its own
	session_save(true);
}

static void run_export_job(void)
{
	char exportPath[EXPORT_PATH_MAX];
	bool ok = g_exportFormat == EXPORT_ORA ? export_ora(g_drawingName, exportPath, sizeof exportPath)
	                                       : export_png(g_drawingName, exportPath, sizeof exportPath);
	char extension[EXTENSION_TEXT_SIZE];
	snprintf(extension, sizeof extension, " as .%s", export_file_extension(g_exportFormat));
	app_toast_name(ok ? "Exported " : "Export failed: ", g_drawingName, ok ? extension : "");
	mascot_event(ok ? MASCOT_EXPORTED : MASCOT_FAILED);
}

// Opens g_jobDrawingName (SESSION_JOB_OPEN) or starts a new drawing (SESSION_JOB_NEW), after saving the open
// one.
static void run_open_job(int job)
{
	if (g_unsaved && !drawings_save_open()) {
		app_toast_name("Save failed: still on ", g_drawingName, "");
		mascot_event(MASCOT_FAILED);
		return;
	}
	bool wantsOpen = job == SESSION_JOB_OPEN;
	bool opened = wantsOpen && drawings_open(g_jobDrawingName);
	if (wantsOpen && !opened && drawings_exists(g_drawingName)) {
		app_toast_name("Could not open ", g_jobDrawingName, "");
		mascot_event(MASCOT_FAILED);
		return;
	}
	// A new drawing also when the open one is gone (deleted), so that it is not written back.
	bool ok = opened || drawings_new();
	after_document_switch();
	if (!ok) {
		g_unsaved = true; // the new drawing could not be saved: the next save or the exit tries again
		app_toast("Save failed: %s", g_drawingName);
		mascot_event(MASCOT_FAILED);
	} else if (wantsOpen && !opened) {
		app_toast_name("Can't open ", g_jobDrawingName, ": new drawing");
		mascot_event(MASCOT_FAILED);
	} else if (job == SESSION_JOB_NEW) {
		app_toast("New drawing: %s", g_drawingName);
		mascot_event(MASCOT_NEW_DRAWING);
	} else {
		app_toast_name("Opened ", g_drawingName, "");
		mascot_event(MASCOT_HELLO);
	}
	ui_open_sheet(SHEET_NONE);
}

static void run_job(void)
{
	int job = g_pendingJob;
	g_pendingJob = SESSION_JOB_NONE;
	app_request_bottom_redraw();
	if (job == SESSION_JOB_LIST) {
		run_list_job();
	} else if (job == SESSION_JOB_EXPORT) {
		run_export_job();
	} else if (job == SESSION_JOB_SAVE) {
		run_save_job();
	} else if (job == SESSION_JOB_OPEN || job == SESSION_JOB_NEW) {
		run_open_job(job);
	}
}

void session_tick(void)
{
	if (g_pendingJob == SESSION_JOB_NONE) {
		return;
	}
	g_jobDelayFrames--;
	if (g_jobDelayFrames <= 0) {
		run_job();
	}
}

int session_pending_job(void)
{
	return g_pendingJob;
}
