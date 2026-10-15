#include "base64.h"

#define BITS_PER_CHARACTER 6
#define BITS_PER_BYTE 8
#define NOT_BASE64 -1
#define GROUP_CHARACTERS 4 // base64 writes each 3 bytes as 4 characters
#define GROUP_BYTES 3

// The alphabet: A-Z, a-z, 0-9, then + and /.
#define LETTER_COUNT 26
#define DIGIT_COUNT 10
#define LOWERCASE_FIRST LETTER_COUNT
#define DIGITS_FIRST (2 * LETTER_COUNT)
#define PLUS_VALUE (DIGITS_FIRST + DIGIT_COUNT)
#define SLASH_VALUE (PLUS_VALUE + 1)

// The 6-bit value of a base64 character, NOT_BASE64 for any other character.
static int character_value(char character)
{
	if (character >= 'A' && character <= 'Z') {
		return character - 'A';
	}
	if (character >= 'a' && character <= 'z') {
		return character - 'a' + LOWERCASE_FIRST;
	}
	if (character >= '0' && character <= '9') {
		return character - '0' + DIGITS_FIRST;
	}
	if (character == '+') {
		return PLUS_VALUE;
	}
	if (character == '/') {
		return SLASH_VALUE;
	}
	return NOT_BASE64;
}

size_t base64_decoded_size(size_t length)
{
	return (length + GROUP_CHARACTERS - 1) / GROUP_CHARACTERS * GROUP_BYTES;
}

size_t base64_decode(const char *text, size_t length, u8 *out)
{
	size_t written = 0;
	u32 bits = 0;
	int bitCount = 0;
	for (size_t i = 0; i < length; i++) {
		int value = character_value(text[i]);
		if (value == NOT_BASE64) {
			continue;
		}
		bits = (bits << BITS_PER_CHARACTER) | (u32)value;
		bitCount += BITS_PER_CHARACTER;
		if (bitCount >= BITS_PER_BYTE) {
			bitCount -= BITS_PER_BYTE;
			out[written++] = (u8)(bits >> bitCount);
		}
	}
	return written;
}
