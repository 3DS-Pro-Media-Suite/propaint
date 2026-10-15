// Krita preset files (.kpp): a PNG whose "preset" text chunk holds the preset as XML.
#pragma once
#include <3ds.h>
#include <stddef.h>

// The "preset" text of a .kpp file (a tEXt, zTXt or iTXt chunk) as a new NUL-terminated string. NULL when the
// file has none. Caller frees.
char *kpp_file_preset_xml(const u8 *file, size_t fileSize);
