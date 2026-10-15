// The app's FTP server for the drawings folder: switching it on and off, and watching its Wi-Fi.
#pragma once
#include <3ds.h>

void ftp_control_toggle(void); // starts the server, or stops it when it runs
const char *ftp_control_label(void); // "FTP server", or the port while it runs
const char *ftp_control_status(void); // "off", the IP, or that it waits for Wi-Fi
// Once a frame: shows the address again when Wi-Fi comes back, stops the server when it stays away.
void ftp_control_watch(u64 now);
