#include <3ds.h>
#include "touch_samples.h"

// HID shared memory holds a ring of the last touch samples. Each entry is a position word, x | y << 16, and
// a flags word.
#define HID_TOUCH_SECTION 42 // u32 offset of the touch section
#define HID_TOUCH_NEWEST 4 // u32 offset of the newest entry's index in the section
#define HID_TOUCH_ENTRIES 8 // u32 offset of the first entry in the section
#define HID_TOUCH_ENTRY_WORDS 2
#define HID_TOUCH_RING_SIZE TOUCH_SAMPLES_MAX
#define HID_TOUCH_FLAG_DOWN 1
#define HID_TOUCH_X_MASK 0xFFFF
#define HID_TOUCH_Y_SHIFT 16

int touch_samples_read(int *xs, int *ys, bool reset)
{
	static u32 lastIndex = HID_TOUCH_RING_SIZE; // not read yet
	if (!hidSharedMem) {
		return 0;
	}
	vu32 *touchSection = hidSharedMem + HID_TOUCH_SECTION;
	u32 newestIndex = touchSection[HID_TOUCH_NEWEST];
	if (newestIndex >= HID_TOUCH_RING_SIZE) {
		return 0;
	}
	if (reset || lastIndex >= HID_TOUCH_RING_SIZE) {
		lastIndex = newestIndex;
		return 0;
	}
	int count = 0;
	while (lastIndex != newestIndex && count < TOUCH_SAMPLES_MAX) {
		lastIndex = (lastIndex + 1) % HID_TOUCH_RING_SIZE;
		vu32 *entry = touchSection + HID_TOUCH_ENTRIES + lastIndex * HID_TOUCH_ENTRY_WORDS;
		u32 position = entry[0];
		u32 flags = entry[1];
		if (flags & HID_TOUCH_FLAG_DOWN) {
			xs[count] = (int)(position & HID_TOUCH_X_MASK);
			ys[count] = (int)(position >> HID_TOUCH_Y_SHIFT);
			count++;
		}
	}
	return count;
}
