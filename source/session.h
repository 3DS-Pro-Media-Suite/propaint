// The open drawing's session: saving it, Save and exit, and the slow jobs (listing the drawings, opening one,
// a new drawing, exporting). A job runs a few frames after it starts, so its toast is on screen while the app
// is busy.
#pragma once
#include <stdbool.h>

enum {
	SESSION_JOB_NONE,
	SESSION_JOB_LIST, // saves the open drawing first, so that its card is current
	SESSION_JOB_OPEN,
	SESSION_JOB_NEW,
	SESSION_JOB_EXPORT,
	SESSION_JOB_SAVE // the autosave after a pause
};

void session_save(bool quiet); // quiet: no "Saved" toast; a failure always shows
bool session_ready_to_quit(void); // Save and exit: saves, false when the app must stay
bool session_quit_failed(void); // the save at Save and exit failed once: the next one quits anyway
// drawingName: the drawing SESSION_JOB_OPEN opens, else NULL. toast may be NULL.
void session_start_job(int job, const char *drawingName, const char *toast);
void session_start_export(void); // exports in the Export page's format as it is when the job runs
void session_tick(void); // once a frame: runs the waiting job when its delay is over
int session_pending_job(void); // SESSION_JOB_NONE when there is none
