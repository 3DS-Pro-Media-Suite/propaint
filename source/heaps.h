// The heap that malloc has not claimed yet. The memory split at start-up is in heaps.c too.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// True when malloc can grow the heap by size bytes, so a malloc of that size cannot fail. Cheap: nothing is
// allocated. False says nothing: freed blocks may still have the room.
bool heaps_unclaimed_fits(size_t size);
