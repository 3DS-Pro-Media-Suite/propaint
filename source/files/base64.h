// Base64 text, the way Krita stores a brush's texture inside its preset.
#pragma once
#include <3ds.h>
#include <stddef.h>

// The most bytes length characters of base64 decode to.
size_t base64_decoded_size(size_t length);

// Decodes length characters of text into out, which needs room for base64_decoded_size(length) bytes.
// Characters outside the alphabet, such as line breaks and padding, are skipped. Returns the bytes written.
size_t base64_decode(const char *text, size_t length, u8 *out);
