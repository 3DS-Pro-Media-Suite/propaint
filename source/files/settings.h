// The settings file and the brush-in-use file: saving and loading them. Settings and the brush library are
// written once the input has paused, before HOME, an applet or sleep, and at exit.
#pragma once
#include <stdbool.h>

void settings_load(void);
void settings_mark_saved(void); // the files hold the settings and the brush as they are now
bool settings_is_dirty(void); // the settings or the brush library have changes their files do not hold yet
void settings_save(void);
