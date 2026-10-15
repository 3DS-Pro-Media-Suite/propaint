// The stylus on the page: strokes fed to the brush engine, with stroke smoothing and the points held while
// the pressure settles, and the eyedropper sample.
#pragma once

extern int g_stylusX; // where the stylus is on the bottom screen
extern int g_stylusY;

void pen_down(int screenX, int screenY);
void pen_move(int screenX, int screenY);
void pen_up(void);
void pen_pick_color(int screenX, int screenY); // the eyedropper: the composited colour under the stylus
