// Per-frame input: the touch on the page (pan, eyedropper, stroke), the mapped button actions, size repeat,
// Circle Pad panning and the UI's input pass.
#pragma once
#include <3ds.h>
#include <stdbool.h>

bool input_panning(void); // the stylus is panning the view

// Once a frame, with the keys from hidKeysDown, hidKeysHeld and hidKeysUp.
void input_update(u32 pressedKeys, u32 heldKeys, u32 releasedKeys);
