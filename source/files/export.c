// Exporters: OpenRaster .ora (layered) and PNG (flattened).
//
// An .ora is a zip: "mimetype" (stored, first), stack.xml (the layer tree, top first), one PNG per layer in
// data/, a thumbnail and mergedimage.png (the flattened picture). Shape layers are rasterised as the PNG
// export draws them, and the white paper is the bottom layer. Groups become isolated stacks. A clipped layer
// only shows where its base does, so a base and the layers clipped to it become an isolated stack in which
// the clipped ones are svg:src-atop.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "export.h"
#include "ascii.h"
#include "color_units.h"
#include "document.h"
#include "paths.h"
#include "png_writer.h"
#include "render.h"
#include "safe_file.h"
#include "zip_writer.h"

#define OPACITY_FULL 100 // percent

#define MIN_EXPORTED_ALPHA 0.002f // fainter shape samples stay transparent
#define SHAPE_EXPORT_MARGIN 2 // px past the outline and softness, or past the shadow's reach

// OpenRaster
#define ORA_VERSION "0.0.6"
#define ORA_DPI "72"
#define ORA_MIMETYPE "image/openraster"
#define ORA_OP_NORMAL "svg:src-over"
#define ORA_OP_CLIPPED "svg:src-atop"
#define ORA_LAYER_FILE "data/%d.png"
#define ORA_LAYER_FILE_SIZE 32
#define ORA_LAYER_SOURCE " src=\"" ORA_LAYER_FILE "\" x=\"0\" y=\"0\""
#define ORA_LAYER_SOURCE_SIZE 48
#define ORA_THUMBNAIL_MAX 256 // OpenRaster keeps the thumbnail within 256 x 256
#define ORA_THUMBNAIL_WIDTH \
	(CANVAS_WIDTH >= CANVAS_HEIGHT ? ORA_THUMBNAIL_MAX : ORA_THUMBNAIL_MAX * CANVAS_WIDTH / CANVAS_HEIGHT)
#define ORA_THUMBNAIL_HEIGHT \
	(CANVAS_WIDTH >= CANVAS_HEIGHT ? ORA_THUMBNAIL_MAX * CANVAS_HEIGHT / CANVAS_WIDTH : ORA_THUMBNAIL_MAX)
#define TEXT_BUFFER_START_SIZE 4096

// A layer to export. shownArea is where a shape layer can show; it is empty for other layers.
typedef struct {
	const Layer *layer;
	IRect shownArea;
} LayerSource;

typedef struct {
	const u8 *rgb;
	int width;
} RgbImageSource;

// A growing text buffer for the XML.
typedef struct {
	char *text;
	size_t length;
	size_t capacity;
	bool failed;
} TextBuffer;

// One item of an OpenRaster container: a layer, or at the root a whole group (the layers first..last).
typedef struct {
	int first;
	int last;
	bool clipped;
} OraItem;

typedef struct {
	int width;
	int height;
	const char *entryName;
} FlattenedImage;

static const FlattenedImage ORA_FLATTENED_IMAGES[] = {
        {ORA_THUMBNAIL_WIDTH, ORA_THUMBNAIL_HEIGHT, "Thumbnails/thumbnail.png"},
        {CANVAS_WIDTH, CANVAS_HEIGHT, "mergedimage.png"},
};
#define ORA_FLATTENED_IMAGE_COUNT ((int)(sizeof ORA_FLATTENED_IMAGES / sizeof ORA_FLATTENED_IMAGES[0]))

static const char *const BLEND_COMPOSITE_OPS[BLEND_COUNT] = {ORA_OP_NORMAL, "svg:multiply", "svg:plus"};
static const char *const FILE_EXTENSIONS[EXPORT_FORMAT_COUNT] = {[EXPORT_ORA] = "ora", [EXPORT_PNG] = "png"};

const char *export_file_extension(int format)
{
	return FILE_EXTENSIONS[format];
}

// The path of the export of a drawing, and its folder made again (FTP can delete it).
static void make_export_path(const char *name, int format, char *path, size_t pathSize)
{
	snprintf(path, pathSize, EXPORTS_DIR "/%s.%s", name, export_file_extension(format));
	mkdir(EXPORTS_DIR, FOLDER_MODE);
}

// A failed export keeps the old file.
bool export_png(const char *name, char *path, size_t pathSize)
{
	make_export_path(name, EXPORT_PNG, path, pathSize);
	u8 *rgb = render_flattened(CANVAS_WIDTH, CANVAS_HEIGHT);
	size_t pngSize = 0;
	u8 *png = rgb ? png_encode(rgb, CANVAS_WIDTH, CANVAS_HEIGHT, &pngSize) : NULL;
	bool ok = png && safe_file_write(path, png, pngSize, NULL, 0);
	free(rgb);
	free(png);
	return ok;
}

// How far past its bounds a shape layer is sampled: outline and softness, or the shadow's offset and blur
// when they reach further.
static int shape_export_padding(const LayerProps *props)
{
	int padding = props->outlineWidth + props->softness + SHAPE_EXPORT_MARGIN;
	if (props->shadowOn) {
		int shadowReach = abs(props->shadowX) + abs(props->shadowY) + props->shadowBlur + SHAPE_EXPORT_MARGIN;
		if (shadowReach > padding) {
			padding = shadowReach;
		}
	}
	return padding;
}

static void layer_source_init(LayerSource *source, const Layer *layer)
{
	memset(source, 0, sizeof *source);
	source->layer = layer;
	const IRect *bounds = &layer->bounds;
	bool hasShape =
	        layer->props.type == LAYER_SDF && layer->distance && layer->pixels && bounds->x1 > bounds->x0;
	if (!hasShape) {
		return;
	}
	source->shownArea = render_pad_on_page(*bounds, shape_export_padding(&layer->props));
}

// Fully transparent pixels are written as zeros.
static void pixel_layer_row(const Layer *layer, int y, u8 *out)
{
	if (!layer->pixels) {
		return;
	}
	const u8 *row = layer->pixels + (size_t)y * CANVAS_WIDTH * RGBA_PIXEL_BYTES;
	for (int x = 0; x < CANVAS_WIDTH; x++) {
		const u8 *pixel = row + x * RGBA_PIXEL_BYTES;
		if (pixel[RGBA_ALPHA_OFFSET]) {
			memcpy(out + x * RGBA_PIXEL_BYTES, pixel, RGBA_PIXEL_BYTES);
		}
	}
}

// Scales by 255 / alpha once, not color / alpha * 255 as render_store_straight_rgba does: the two orders
// round differently in float, and the exported bytes must stay the same.
static void store_export_pixel(const float premultiplied[RGB_CHANNELS], float alpha, u8 *pixel)
{
	float scale = UNIT_TO_BYTE / alpha;
	for (int c = 0; c < RGB_CHANNELS; c++) {
		float value = premultiplied[c] * scale + 0.5f;
		pixel[c] = value > 255 ? 255 : (u8)value;
	}
	pixel[RGBA_ALPHA_OFFSET] = (u8)(alpha > 1 ? 255 : alpha * UNIT_TO_BYTE + 0.5f);
}

// The renderer's premultiplied samples, turned into straight-alpha bytes.
static void shape_layer_row(const LayerSource *source, int y, u8 *out)
{
	const IRect *area = &source->shownArea;
	if (y < area->y0 || y >= area->y1) {
		return;
	}
	for (int x = area->x0; x < area->x1; x++) {
		float premultiplied[RGB_CHANNELS];
		float alpha;
		render_layer_sample(source->layer, x, y, premultiplied, &alpha);
		if (alpha > MIN_EXPORTED_ALPHA) {
			store_export_pixel(premultiplied, alpha, out + x * RGBA_PIXEL_BYTES);
		}
	}
}

static void layer_row(const void *context, int y, u8 *out)
{
	const LayerSource *source = context;
	memset(out, 0, (size_t)CANVAS_WIDTH * RGBA_PIXEL_BYTES);
	if (source->layer->props.type == LAYER_PIXEL) {
		pixel_layer_row(source->layer, y, out);
		return;
	}
	shape_layer_row(source, y, out);
}

static void paper_row(const void *context, int y, u8 *out)
{
	memset(out, 255, (size_t)CANVAS_WIDTH * RGB_PIXEL_BYTES);
}

static void rgb_image_row(const void *context, int y, u8 *out)
{
	const RgbImageSource *source = context;
	memcpy(out, source->rgb + (size_t)y * source->width * RGB_PIXEL_BYTES,
	       (size_t)source->width * RGB_PIXEL_BYTES);
}

// stack.xml

// printf at the end of the buffer, growing it as needed. On failure, failed is set.
static void text_append(TextBuffer *buffer, const char *format, ...)
{
	va_list args;
	for (;;) {
		va_start(args, format);
		char *end = buffer->text ? buffer->text + buffer->length : NULL;
		size_t room = buffer->text ? buffer->capacity - buffer->length : 0;
		int written = vsnprintf(end, room, format, args);
		va_end(args);
		if (written < 0) {
			buffer->failed = true;
			return;
		}
		if (buffer->text && buffer->length + written < buffer->capacity) {
			buffer->length += written;
			return;
		}
		size_t newCapacity = (buffer->capacity ? buffer->capacity * 2 : TEXT_BUFFER_START_SIZE) + written;
		char *grown = realloc(buffer->text, newCapacity);
		if (!grown) {
			buffer->failed = true;
			return;
		}
		buffer->text = grown;
		buffer->capacity = newCapacity;
	}
}

static const char *ora_composite_op(int blend)
{
	return blend < BLEND_COUNT ? BLEND_COMPOSITE_OPS[blend] : ORA_OP_NORMAL;
}

// Names are at most LAYER_NAME_SIZE bytes; only printable ASCII is kept.
static void xml_append_name(TextBuffer *xml, const char *name)
{
	for (int i = 0; i < LAYER_NAME_SIZE && name[i]; i++) {
		unsigned char c = (unsigned char)name[i];
		if (c == '&') {
			text_append(xml, "&amp;");
		} else if (c == '<') {
			text_append(xml, "&lt;");
		} else if (c == '>') {
			text_append(xml, "&gt;");
		} else if (c == '"') {
			text_append(xml, "&quot;");
		} else if (ascii_is_printable(name[i])) {
			text_append(xml, "%c", c);
		}
	}
}

// The attributes of every layer and stack. extra goes between the name and the opacity.
static void ora_attributes(TextBuffer *xml, const char *name, const char *extra, int opacity, bool visible,
                           const char *compositeOp)
{
	text_append(xml, " name=\"");
	xml_append_name(xml, name);
	if (opacity > OPACITY_FULL) {
		opacity = OPACITY_FULL;
	}
	// The file wants the opacity percent as 0.000 to 1.000.
	text_append(xml, "\"%s opacity=\"%d.%03d\" visibility=\"%s\" composite-op=\"%s\"", extra, opacity / 100,
	            opacity % 100 * 10, visible ? "visible" : "hidden", compositeOp);
}

static void ora_stack_open(TextBuffer *xml, const char *name, int opacity, bool visible,
                           const char *compositeOp)
{
	text_append(xml, "<stack");
	ora_attributes(xml, name, "", opacity, visible, compositeOp);
	text_append(xml, " isolation=\"isolate\">\n");
}

// A layer whose image is data/<fileIndex>.png.
static void ora_layer(TextBuffer *xml, const char *name, int fileIndex, int opacity, bool visible,
                      const char *compositeOp)
{
	char source[ORA_LAYER_SOURCE_SIZE];
	snprintf(source, sizeof source, ORA_LAYER_SOURCE, fileIndex);
	text_append(xml, "<layer");
	ora_attributes(xml, name, source, opacity, visible, compositeOp);
	text_append(xml, "/>\n");
}

static void ora_container(TextBuffer *xml, int bottom, int top, bool atRoot);

// compositeOp, when not NULL, replaces the item's own blend mode.
static void ora_item(TextBuffer *xml, const OraItem *item, bool atRoot, const char *compositeOp)
{
	int groupId = g_document.layers[item->first].props.group;
	if (atRoot && groupId) {
		const Group *group = &g_document.groups[groupId];
		const char *groupOp = compositeOp ? compositeOp : ora_composite_op(group->blend);
		ora_stack_open(xml, group->name, group->opacity, group->visible, groupOp);
		ora_container(xml, item->first, item->last, false);
		text_append(xml, "</stack>\n");
		return;
	}
	const LayerProps *props = &g_document.layers[item->first].props;
	const char *layerOp = compositeOp ? compositeOp : ora_composite_op(props->blend);
	ora_layer(xml, props->name, item->first, props->opacity, props->visible, layerOp);
}

// The items of the layers bottom..top, bottom first. At the root a group is one item and is never clipped.
static int ora_collect_items(int bottom, int top, bool atRoot, OraItem items[MAX_LAYERS])
{
	int count = 0;
	int layer = bottom;
	while (layer <= top && count < MAX_LAYERS) {
		int groupId = g_document.layers[layer].props.group;
		bool wholeGroup = atRoot && groupId;
		int last = layer;
		if (wholeGroup) {
			while (last < top && g_document.layers[last + 1].props.group == groupId) {
				last++;
			}
		}
		items[count].first = layer;
		items[count].last = last;
		items[count].clipped = !wholeGroup && g_document.layers[layer].props.clipped;
		count++;
		layer = last + 1;
	}
	return count;
}

// A base and the items clipped to it (base + 1 .. topClipped) as one isolated stack. base is -1 when the
// clipped items have nothing below them.
static void ora_clip_stack(TextBuffer *xml, const OraItem *items, int base, int topClipped, bool atRoot)
{
	int namedItem = base >= 0 ? base : 0;
	int groupId = atRoot ? g_document.layers[items[namedItem].first].props.group : 0;
	const char *name =
	        groupId ? g_document.groups[groupId].name : g_document.layers[items[namedItem].first].props.name;
	int blend = BLEND_NORMAL;
	if (base >= 0) {
		blend = groupId ? g_document.groups[groupId].blend : g_document.layers[items[base].first].props.blend;
	}
	ora_stack_open(xml, name, OPACITY_FULL, true, ora_composite_op(blend));
	for (int i = topClipped; i > base; i--) {
		ora_item(xml, &items[i], atRoot, ORA_OP_CLIPPED);
	}
	if (base >= 0) {
		ora_item(xml, &items[base], atRoot, ORA_OP_NORMAL);
	}
	text_append(xml, "</stack>\n");
}

// The root (where a group is one item) or one group's layers, top first. A clipped layer clips to the nearest
// unclipped item below it in the same container, as on screen. A clip run becomes an isolated stack with
// src-atop, so in the file clipped layers lose their own blend mode.
static void ora_container(TextBuffer *xml, int bottom, int top, bool atRoot)
{
	OraItem items[MAX_LAYERS];
	int count = ora_collect_items(bottom, top, atRoot, items);
	int i = count - 1;
	while (i >= 0) {
		int base = i; // the unclipped item at or below i; -1 when there is none
		while (base >= 0 && items[base].clipped) {
			base--;
		}
		if (base == i) {
			ora_item(xml, &items[i], atRoot, NULL);
		} else {
			ora_clip_stack(xml, items, base, i, atRoot);
		}
		i = base - 1;
	}
}

static void ora_write_stack_xml(TextBuffer *xml)
{
	text_append(xml,
	            "<?xml version='1.0' encoding='UTF-8'?>\n<image version=\"" ORA_VERSION
	            "\" w=\"%d\" h=\"%d\" xres=\"" ORA_DPI "\" yres=\"" ORA_DPI "\">\n<stack>\n",
	            CANVAS_WIDTH, CANVAS_HEIGHT);
	if (g_document.layerCount > 0) {
		ora_container(xml, 0, g_document.layerCount - 1, true);
	}
	ora_layer(xml, "Paper", g_document.layerCount, OPACITY_FULL, true, ORA_OP_NORMAL);
	text_append(xml, "</stack>\n</image>\n");
}

// One RGBA image per layer, then the white paper in RGB.
static void ora_add_layer_images(Zip *zip)
{
	char entryName[ORA_LAYER_FILE_SIZE];
	for (int i = 0; i < g_document.layerCount && zip->ok; i++) {
		LayerSource source;
		layer_source_init(&source, &g_document.layers[i]);
		snprintf(entryName, sizeof entryName, ORA_LAYER_FILE, i);
		png_write_zip_entry(zip, entryName, CANVAS_WIDTH, CANVAS_HEIGHT, RGBA_PIXEL_BYTES, layer_row,
		                    &source);
	}
	snprintf(entryName, sizeof entryName, ORA_LAYER_FILE, g_document.layerCount);
	png_write_zip_entry(zip, entryName, CANVAS_WIDTH, CANVAS_HEIGHT, RGB_PIXEL_BYTES, paper_row, NULL);
}

static void ora_add_flattened_images(Zip *zip)
{
	for (int i = 0; i < ORA_FLATTENED_IMAGE_COUNT && zip->ok; i++) {
		const FlattenedImage *image = &ORA_FLATTENED_IMAGES[i];
		u8 *rgb = render_flattened(image->width, image->height);
		if (!rgb) {
			zip->ok = false;
			return;
		}
		RgbImageSource source = {rgb, image->width};
		png_write_zip_entry(zip, image->entryName, image->width, image->height, RGB_PIXEL_BYTES,
		                    rgb_image_row, &source);
		free(rgb);
	}
}

// Written through a temporary file, so a failed export keeps the old file.
bool export_ora(const char *name, char *path, size_t pathSize)
{
	make_export_path(name, EXPORT_ORA, path, pathSize);
	char tempPath[SAFE_FILE_PATH_SIZE];
	safe_file_temp_path(path, tempPath);
	TextBuffer stackXml = {0};
	ora_write_stack_xml(&stackXml);
	Zip zip;
	if (stackXml.failed || !zip_open(&zip, tempPath)) {
		free(stackXml.text);
		return false;
	}
	render_prep();
	zip_add_file(&zip, "mimetype", ORA_MIMETYPE, sizeof ORA_MIMETYPE - 1, false);
	zip_add_file(&zip, "stack.xml", stackXml.text, stackXml.length, true);
	free(stackXml.text);
	ora_add_layer_images(&zip);
	ora_add_flattened_images(&zip);
	bool written = zip_close(&zip);
	return safe_file_commit_or_delete(tempPath, path, written);
}
