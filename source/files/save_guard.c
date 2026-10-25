#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "save_guard.h"
#include "safe_file.h"

static LightLock g_lock; // the FTP session threads read the paths while the main thread sets them
static char g_path[SAFE_FILE_PATH_SIZE]; // "" while no file is written
static char g_tempPath[SAFE_FILE_PATH_SIZE];

void save_guard_init(void)
{
	LightLock_Init(&g_lock);
}

void save_guard_begin(const char *path)
{
	LightLock_Lock(&g_lock);
	snprintf(g_path, sizeof g_path, "%s", path);
	safe_file_temp_path(path, g_tempPath);
	LightLock_Unlock(&g_lock);
}

void save_guard_end(void)
{
	LightLock_Lock(&g_lock);
	g_path[0] = '\0';
	g_tempPath[0] = '\0';
	LightLock_Unlock(&g_lock);
}

// The card is FAT, so a name matches in any case.
static bool is_or_holds(const char *cardPath, const char *path)
{
	size_t length = strlen(cardPath);
	bool startsWith = path[0] && !strncasecmp(path, cardPath, length);
	return startsWith && (path[length] == '\0' || path[length] == '/');
}

bool save_guard_busy(const char *cardPath)
{
	LightLock_Lock(&g_lock);
	bool busy = is_or_holds(cardPath, g_path) || is_or_holds(cardPath, g_tempPath);
	LightLock_Unlock(&g_lock);
	return busy;
}
