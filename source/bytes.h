// Little- and big-endian numbers in byte buffers, as file formats store them.
#pragma once
#include <3ds.h>

#define BITS_PER_BYTE 8

static inline u16 bytes_read_le16(const u8 *bytes)
{
	return (u16)(bytes[0] | bytes[1] << BITS_PER_BYTE);
}

static inline u32 bytes_read_le32(const u8 *bytes)
{
	return (u32)bytes[0] | (u32)bytes[1] << BITS_PER_BYTE | (u32)bytes[2] << (2 * BITS_PER_BYTE) |
	       (u32)bytes[3] << (3 * BITS_PER_BYTE);
}

static inline u32 bytes_read_be32(const u8 *bytes)
{
	return (u32)bytes[0] << (3 * BITS_PER_BYTE) | (u32)bytes[1] << (2 * BITS_PER_BYTE) |
	       (u32)bytes[2] << BITS_PER_BYTE | bytes[3];
}

static inline void bytes_write_le16(u8 *destination, u16 value)
{
	destination[0] = value;
	destination[1] = value >> BITS_PER_BYTE;
}

static inline void bytes_write_le32(u8 *destination, u32 value)
{
	destination[0] = value;
	destination[1] = value >> BITS_PER_BYTE;
	destination[2] = value >> (2 * BITS_PER_BYTE);
	destination[3] = value >> (3 * BITS_PER_BYTE);
}

static inline void bytes_write_be32(u8 *destination, u32 value)
{
	destination[0] = value >> (3 * BITS_PER_BYTE);
	destination[1] = value >> (2 * BITS_PER_BYTE);
	destination[2] = value >> BITS_PER_BYTE;
	destination[3] = value;
}
