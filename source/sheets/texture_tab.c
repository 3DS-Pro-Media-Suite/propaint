#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "texture_tab.h"
#include "app_state.h"
#include "brush_engine.h"
#include "color_units.h"
#include "draw.h"
#include "framebuffer.h"
#include "paths.h"
#include "textures.h"
#include "ui.h"
#include "widgets.h"

#define SLIDER_ROW_HEIGHT 30
#define LIST_BOTTOM_MARGIN 2
#define LIST_COLUMNS 2
#define TILE_HEIGHT 28
#define TILE_ROW_STEP 30
#define SWATCH_INSET 2
#define SWATCH_SIZE 24 // px of the texture at its own size
#define SWATCH_BYTES (SWATCH_SIZE * SWATCH_SIZE * RGB_PIXEL_BYTES)
#define UNREADABLE_SWATCH_GREY 0.2f // a texture file that cannot be read
#define TILE_TEXT_GAP 4
#define TILE_TEXT_X (SWATCH_INSET + SWATCH_SIZE + TILE_TEXT_GAP)
#define TILE_TEXT_RIGHT_MARGIN 4
#define TILE_NAME_Y (-1)
#define TILE_SOURCE_Y 12
#define HINT_ROW_HEIGHT 20
#define HINT_TEXT_Y 3
#define STRENGTH_ON_PICK 60 // picking a texture for a brush without one makes it show
#define NOISE_TILE (-1) // the list's first tile: the built-in noise, before the textures
#define TILE_COUNT_MAX (TEXTURES_MAX + 1)
#define PNG_EXTENSION_LENGTH 4 // ".png"

static const char *const SOURCE_LABELS[] = {
        [TEXTURE_FROM_APP] = "from Krita",
        [TEXTURE_FROM_SD_CARD] = "SD card",
};

// The tiles' swatches, made when first shown.
static u8 g_swatches[TILE_COUNT_MAX][SWATCH_BYTES];
static bool g_swatchReady[TILE_COUNT_MAX];
static int g_swatchedTextureCount = -1; // the texture count the swatches were made for

void texture_tab_opened(void)
{
	textures_scan();
	g_swatchedTextureCount = -1;
}

// A texture's file name as words: without ".png" and a leading sort number such as "03_", with spaces for
// underscores and dashes.
static void shown_name(const char *fileName, char *out, size_t outSize)
{
	int length = (int)strlen(fileName) - PNG_EXTENSION_LENGTH;
	int start = 0;
	while (start < length && isdigit((unsigned char)fileName[start])) {
		start++;
	}
	bool sortNumber = start > 0 && start < length && (fileName[start] == '_' || fileName[start] == '-');
	start = sortNumber ? start + 1 : 0;
	snprintf(out, outSize, "%.*s", length > start ? length - start : 0, fileName + start);
	for (char *letter = out; *letter; letter++) {
		if (*letter == '_' || *letter == '-') {
			*letter = ' ';
		}
	}
}

static void fill_swatch_pixel(u8 *swatch, int x, int y, float value)
{
	u8 grey = (u8)(value * UNIT_TO_BYTE + 0.5f);
	u8 *pixel = swatch + ((size_t)y * SWATCH_SIZE + x) * RGB_PIXEL_BYTES;
	pixel[0] = grey;
	pixel[1] = grey;
	pixel[2] = grey;
}

// The swatch shows the texture at its own size, from its top left corner.
static void make_swatch(int tile, u8 *swatch)
{
	bool isNoise = tile == NOISE_TILE;
	const TextureTile *texture = isNoise ? NULL : textures_tile(textures_name(tile));
	for (int y = 0; y < SWATCH_SIZE; y++) {
		for (int x = 0; x < SWATCH_SIZE; x++) {
			float value = isNoise ? brush_noise_texture(x, y) : UNREADABLE_SWATCH_GREY;
			if (texture) {
				int tileX = x % texture->width;
				int tileY = y % texture->height;
				value = texture->pixels[tileY * texture->width + tileX] * BYTE_TO_UNIT;
			}
			fill_swatch_pixel(swatch, x, y, value);
		}
	}
}

// NULL while the swatch waits for a later frame: one is made per frame, because each reads a PNG.
static const u8 *swatch(int tile, int *budget)
{
	if (g_swatchedTextureCount != textures_count()) {
		memset(g_swatchReady, 0, sizeof g_swatchReady);
		g_swatchedTextureCount = textures_count();
	}
	int slot = tile + 1;
	if (!g_swatchReady[slot]) {
		if (*budget <= 0) {
			app_request_bottom_redraw();
			return NULL;
		}
		(*budget)--;
		make_swatch(tile, g_swatches[slot]);
		g_swatchReady[slot] = true;
	}
	return g_swatches[slot];
}

static void pick(int tile)
{
	const char *name = tile == NOISE_TILE ? "" : textures_name(tile);
	snprintf(g_brush.texture, sizeof g_brush.texture, "%s", name);
	if (!g_brush.textureStrength) {
		g_brush.textureStrength = STRENGTH_ON_PICK;
	}
	char shown[BRUSH_TEXTURE_NAME_SIZE];
	shown_name(name, shown, sizeof shown);
	app_toast_name("Texture: ", tile == NOISE_TILE ? "noise" : shown, "");
}

static void texture_tile(int tile, int x, int y, int *budget)
{
	bool isNoise = tile == NOISE_TILE;
	const char *name = isNoise ? "" : textures_name(tile);
	if (g_uiSurface) {
		bool selected = !strcmp(g_brush.texture, name);
		draw_rounded_rect(g_uiSurface, x, y, SHEET_TILE_WIDTH, TILE_HEIGHT,
		                  selected ? COLOR_ACCENT_DARK : COLOR_WIDGET);
		const u8 *pixels = swatch(tile, budget);
		if (pixels) {
			draw_image(g_uiSurface, x + SWATCH_INSET, y + SWATCH_INSET, pixels, SWATCH_SIZE, SWATCH_SIZE);
		}
		char shown[BRUSH_TEXTURE_NAME_SIZE];
		shown_name(name, shown, sizeof shown);
		char fitted[BRUSH_TEXTURE_NAME_SIZE];
		draw_fit_text(fitted, sizeof fitted, isNoise ? "Noise" : shown,
		              SHEET_TILE_WIDTH - TILE_TEXT_X - TILE_TEXT_RIGHT_MARGIN);
		int textX = x + TILE_TEXT_X;
		draw_text(g_uiSurface, FONT_SMALL, textX, y + TILE_NAME_Y, COLOR_TEXT, ALIGN_LEFT, "%s", fitted);
		const char *source = isNoise ? "built in" : SOURCE_LABELS[textures_source(tile)];
		draw_text(g_uiSurface, FONT_SMALL, textX, y + TILE_SOURCE_Y, COLOR_DIM, ALIGN_LEFT, "%s", source);
	}
	if (widget_clicked(x, y, SHEET_TILE_WIDTH, TILE_HEIGHT)) {
		pick(tile);
	}
}

static void sliders(int sheetLeft, int y)
{
	int strength = g_brush.textureStrength;
	if (widget_slider(WIDGET_BRUSH_TEXTURE_STRENGTH, sheetLeft + SHEET_LEFT_COLUMN_X, y, SHEET_COLUMN_WIDTH,
	                  &strength, 0, 100, true, "Strength", draw_format("%d%%", g_brush.textureStrength))) {
		g_brush.textureStrength = (u8)strength;
	}
	int scale = g_brush.textureScale ? g_brush.textureScale : BRUSH_TEXTURE_SCALE_DEFAULT;
	if (widget_slider(WIDGET_BRUSH_TEXTURE_SCALE, sheetLeft + SHEET_RIGHT_COLUMN_X, y, SHEET_COLUMN_WIDTH,
	                  &scale, BRUSH_TEXTURE_SCALE_MIN, BRUSH_TEXTURE_SCALE_MAX, true, "Size",
	                  draw_format("%d%%", scale))) {
		g_brush.textureScale = (u8)scale;
	}
}

void texture_tab(int sheetLeft, int y)
{
	static ScrollList listScroll;
	sliders(sheetLeft, y);
	int listTop = y + SLIDER_ROW_HEIGHT;
	int listHeight = SCREEN_HEIGHT - LIST_BOTTOM_MARGIN - listTop;
	int tileCount = textures_count() + 1;
	int rowCount = (tileCount + LIST_COLUMNS - 1) / LIST_COLUMNS;
	int contentHeight = rowCount * TILE_ROW_STEP + HINT_ROW_HEIGHT;
	widget_scroll_input(&listScroll, WIDGET_TEXTURE_LIST, sheetLeft, listTop, SHEET_WIDTH, listHeight,
	                    contentHeight);
	int swatchBudget = 1;
	draw_set_clip(listTop, listTop + listHeight);
	for (int i = 0; i < tileCount; i++) {
		int tileX = sheetLeft + SHEET_TILE_X + (i % LIST_COLUMNS) * SHEET_TILE_STEP;
		int tileY = listTop + (i / LIST_COLUMNS) * TILE_ROW_STEP - listScroll.offset;
		bool outOfView = tileY + TILE_ROW_STEP <= listTop || tileY >= listTop + listHeight;
		if (!outOfView) {
			texture_tile(i - 1, tileX, tileY, &swatchBudget);
		}
	}
	if (g_uiSurface) {
		int hintY = listTop + rowCount * TILE_ROW_STEP - listScroll.offset + HINT_TEXT_Y;
		draw_text(g_uiSurface, FONT_SMALL, sheetLeft + SHEET_LEFT_COLUMN_X, hintY, COLOR_DIM, ALIGN_LEFT,
		          "Your PNGs: " TEXTURES_FOLDER);
	}
	draw_reset_clip();
	widget_scrollbar(sheetLeft + SHEET_SCROLLBAR_X, listTop, listHeight, contentHeight, listScroll.offset);
}
