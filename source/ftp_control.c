#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ftp_control.h"
#include "app_state.h"
#include "ftp.h"
#include "mascot.h"
#include "paths.h"
#include "save_guard.h"

#define FTP_PORT 5421
#define FTP_WIFI_WAIT_MS 30000 // after a sleep or Wi-Fi loss, how long the server may wait for the network
#define TILE_TEXT_SIZE 16 // "255.255.255.255" and its terminating zero

static const FtpConfig FTP_CONFIG = {
        .greeting = "Pro Paint drawings",
        .rootPath = DRAWINGS_DIR,
        .port = FTP_PORT,
        .removeFolderContents = true,
        .canDelete = NULL,
        .isBusy = save_guard_busy, // a drawing or an export may be in the middle of a save
};

static u64 g_ftpOfflineSince; // when the running server lost its network; 0 while it has one

// While the server runs, the label names the port and the status shows the IP alone: "ip:port" is too wide.
const char *ftp_control_label(void)
{
	static char label[TILE_TEXT_SIZE];
	if (!ftp_running()) {
		return "FTP server";
	}
	snprintf(label, sizeof label, "FTP port %d", FTP_PORT);
	return label;
}

const char *ftp_control_status(void)
{
	static char ip[TILE_TEXT_SIZE];
	if (!ftp_running()) {
		return "off";
	}
	const char *address = ftp_address();
	if (!address) {
		return "no Wi-Fi yet";
	}
	int ipLength = (int)strcspn(address, ":");
	snprintf(ip, sizeof ip, "%.*s", ipLength, address);
	return ip;
}

void ftp_control_toggle(void)
{
	if (ftp_running()) {
		ftp_stop();
		app_toast("FTP stopped");
	} else if (ftp_start(&FTP_CONFIG)) {
		app_toast("ftp://%s", ftp_address());
		mascot_event(MASCOT_HELLO);
	} else {
		app_toast("No Wi-Fi: FTP not started");
		mascot_event(MASCOT_SAD);
	}
	g_ftpOfflineSince = 0;
}

// The server waits for Wi-Fi by itself after a sleep; the app gives up on it after FTP_WIFI_WAIT_MS.
void ftp_control_watch(u64 now)
{
	if (!ftp_running()) {
		return;
	}
	bool online = ftp_address() != NULL;
	if (online && g_ftpOfflineSince) {
		g_ftpOfflineSince = 0;
		app_toast("ftp://%s", ftp_address());
		app_request_bottom_redraw();
	} else if (!online && !g_ftpOfflineSince) {
		g_ftpOfflineSince = now;
		app_request_bottom_redraw();
	} else if (!online && now - g_ftpOfflineSince > FTP_WIFI_WAIT_MS) {
		ftp_stop();
		g_ftpOfflineSince = 0;
		app_toast("FTP stopped: no Wi-Fi");
		mascot_event(MASCOT_SAD);
		app_request_bottom_redraw();
	}
}
