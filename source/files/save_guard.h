// The file the app is writing in the drawings folder. The FTP server asks before it deletes anything, so a
// client cannot delete a drawing or an export, or the temporary file it goes through, in the middle of a save.
#pragma once
#include <stdbool.h>

void save_guard_init(void); // once at start-up, before the FTP server can start
// Main thread, around one save. path is the file's final path; its temporary file is covered too.
void save_guard_begin(const char *path);
void save_guard_end(void);
// From any thread: true when cardPath is the file being written, its temporary file or a folder holding them.
bool save_guard_busy(const char *cardPath);
