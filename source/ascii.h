// The printable ASCII range: the font has a glyph for each of these characters, and names keep only these.
#pragma once
#include <stdbool.h>

#define ASCII_PRINTABLE_FIRST ' '
#define ASCII_PRINTABLE_LAST '~'

static inline bool ascii_is_printable(char character)
{
	unsigned char code = (unsigned char)character;
	return code >= ASCII_PRINTABLE_FIRST && code <= ASCII_PRINTABLE_LAST;
}
