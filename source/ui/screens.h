// Composing the bottom and top framebuffers each frame: the page, the size ring, the loupe, the UI, the
// toast, the layer panel, the brush status and the mascot. A screen is composed again only when something on
// it can have changed; otherwise its last picture is copied.
#pragma once
#include <3ds.h>

// Once a frame, with the keys from hidKeysDown and hidKeysUp: composes both screens and shows them.
void screens_draw(u32 pressedKeys, u32 releasedKeys);
