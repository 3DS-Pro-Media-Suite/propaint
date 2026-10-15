// The zip file format, shared by the reader and the writer: little-endian records with these signatures,
// sizes and field offsets.
#pragma once

#define ZIP_METHOD_STORED 0
#define ZIP_METHOD_DEFLATED 8

// The local header in front of each entry's data.
#define ZIP_LOCAL_SIGNATURE 0x04034b50u
#define ZIP_LOCAL_HEADER_SIZE 30
#define ZIP_LOCAL_VERSION_NEEDED_OFFSET 4
#define ZIP_LOCAL_METHOD_OFFSET 8
#define ZIP_LOCAL_TIME_OFFSET 10
#define ZIP_LOCAL_DATE_OFFSET 12
#define ZIP_LOCAL_CRC_OFFSET 14
#define ZIP_LOCAL_NAME_LENGTH_OFFSET 26
#define ZIP_LOCAL_EXTRA_LENGTH_OFFSET 28

// One entry of the central directory.
#define ZIP_CENTRAL_SIGNATURE 0x02014b50u
#define ZIP_CENTRAL_HEADER_SIZE 46
#define ZIP_CENTRAL_VERSION_MADE_BY_OFFSET 4
#define ZIP_CENTRAL_VERSION_NEEDED_OFFSET 6
#define ZIP_CENTRAL_METHOD_OFFSET 10
#define ZIP_CENTRAL_TIME_OFFSET 12
#define ZIP_CENTRAL_DATE_OFFSET 14
#define ZIP_CENTRAL_CRC_OFFSET 16
#define ZIP_CENTRAL_NAME_LENGTH_OFFSET 28
#define ZIP_CENTRAL_EXTRA_LENGTH_OFFSET 30
#define ZIP_CENTRAL_COMMENT_LENGTH_OFFSET 32
#define ZIP_CENTRAL_LOCAL_HEADER_OFFSET 42

// The crc, compressed size and uncompressed size follow each other in both kinds of entry header.
#define ZIP_CRC_FIELD_OFFSET 0
#define ZIP_COMPRESSED_SIZE_FIELD_OFFSET 4
#define ZIP_UNCOMPRESSED_SIZE_FIELD_OFFSET 8
#define ZIP_CRC_AND_SIZES_SIZE 12

// The end of central directory record.
#define ZIP_END_SIGNATURE 0x06054b50u
#define ZIP_END_RECORD_SIZE 22
#define ZIP_END_DISK_ENTRY_COUNT_OFFSET 8
#define ZIP_END_TOTAL_ENTRY_COUNT_OFFSET 10
#define ZIP_END_DIRECTORY_SIZE_OFFSET 12
#define ZIP_END_DIRECTORY_OFFSET_OFFSET 16
