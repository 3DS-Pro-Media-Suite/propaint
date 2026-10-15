// The folders on the SD card. A *_FOLDER is the path the user sees, the *_DIR beside it adds the card prefix.
#pragma once

#define SD_CARD "sdmc:"
#define HOMEBREW_FOLDER "/3ds"
#define HOMEBREW_DIR SD_CARD HOMEBREW_FOLDER
#define APP_FOLDER HOMEBREW_FOLDER "/propaint"
#define APP_DIR SD_CARD APP_FOLDER
#define BRUSH_IMPORT_FOLDER APP_FOLDER "/brushes" // drop Krita .kpp / .bundle files here
#define BRUSH_IMPORT_DIR SD_CARD BRUSH_IMPORT_FOLDER
#define TEXTURES_FOLDER APP_FOLDER "/textures" // brush textures: PNG files
#define TEXTURES_DIR SD_CARD TEXTURES_FOLDER
#define DRAWINGS_FOLDER "/Drawings"
#define DRAWINGS_DIR SD_CARD DRAWINGS_FOLDER
#define EXPORTS_FOLDER DRAWINGS_FOLDER "/Exports"
#define EXPORTS_DIR SD_CARD EXPORTS_FOLDER

#define FOLDER_MODE 0777 // the permissions mkdir gives a new folder
