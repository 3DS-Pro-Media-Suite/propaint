// The memory split at start-up. Only the framebuffers use linear memory, so the layers get the rest, up to
// what the heap area holds. libctru's own split gives the heap all but the linear heap, which on a New 3DS
// is more than the heap area, and the app would not start.
#include <3ds.h>
#include "math_util.h"

#define LINEAR_HEAP_MIN_SIZE (8 * MEBIBYTE)
#define HEAP_AREA_SIZE (OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN)
#define PAGE_MASK 0xFFF

// Set up by libctru and newlib, used again when the app exits.
extern char *fake_heap_start;
extern char *fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

static u32 free_memory(void)
{
	Handle limit = 0;
	if (R_FAILED(svcGetResourceLimit(&limit, CUR_PROCESS_HANDLE))) {
		svcBreak(USERBREAK_PANIC);
	}
	ResourceLimitType type = RESLIMIT_COMMIT;
	s64 maxCommit = 0;
	s64 currentCommit = 0;
	svcGetResourceLimitLimitValues(&maxCommit, limit, &type, 1);
	svcGetResourceLimitCurrentValues(&currentCommit, limit, &type, 1);
	svcCloseHandle(limit);
	return (u32)(maxCommit - currentCommit) & ~PAGE_MASK;
}

// Replaces libctru's weak version, which runs before main.
void __system_allocateHeaps(void)
{
	u32 remaining = free_memory();
	__ctru_heap_size = remaining - LINEAR_HEAP_MIN_SIZE;
	if (__ctru_heap_size > HEAP_AREA_SIZE) {
		__ctru_heap_size = HEAP_AREA_SIZE;
	}
	__ctru_linear_heap_size = remaining - __ctru_heap_size;
	Result heapResult = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0, __ctru_heap_size, MEMOP_ALLOC,
	                                     MEMPERM_READ | MEMPERM_WRITE);
	if (R_FAILED(heapResult)) {
		svcBreak(USERBREAK_PANIC);
	}
	Result linearResult = svcControlMemory(&__ctru_linear_heap, 0, 0, __ctru_linear_heap_size,
	                                       MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE);
	if (R_FAILED(linearResult)) {
		svcBreak(USERBREAK_PANIC);
	}
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	fake_heap_start = (char *)__ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;
}
