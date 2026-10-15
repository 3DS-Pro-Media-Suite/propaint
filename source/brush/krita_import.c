// Krita preset import. A .kpp file is a PNG whose "preset" text chunk holds the preset as XML, with its
// settings in <param name="..."> elements. A .bundle is a zip archive of .kpp files. Only what this engine
// can do is mapped: the auto-brush tip, spacing, opacity, flow, scatter, the texture with its strength and
// scale, the colour smudge engine's paint, and the sensors that drive size, opacity and flow. Everything else
// is approximated or ignored.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "krita_import.h"
#include "brush_engine.h"
#include "brush_library.h"
#include "kpp_file.h"
#include "base64.h"
#include "math_util.h"
#include "paths.h"
#include "textures.h"
#include "zip_reader.h"

#define MAX_READ_FILE_BYTES (24 * MEBIBYTE)

#define MAX_COMPRESSED_PRESET_BYTES (4 * MEBIBYTE)
#define MAX_PRESET_BYTES (8 * MEBIBYTE)

#define CDATA_START "<![CDATA["
#define CDATA_END "]]>"
#define PARAM_END "</param>"
#define PARAM_KEY_SIZE 80
#define ATTRIBUTE_KEY_SIZE 40
#define NUMBER_TEXT_SIZE 64
#define BOOL_TEXT_SIZE 16

// One sample per entry of Brush.sizeCurve, evenly spaced over 0..1.
#define CURVE_SAMPLE_STEP (1.0f / (BRUSH_CURVE_POINTS - 1))
#define CURVE_MAX_POINTS 32
#define CURVE_STRAIGHT_TOLERANCE 3
#define CURVE_TAG "<curve>"

#define SENSOR_PARAM_NAME_SIZE 48
#define SENSOR_XML_SIZE 8192
#define ROTATION_SENSOR_XML_SIZE 4096
#define TIP_DEFINITION_SIZE 16384
#define PRESET_NAME_SIZE 64
#define COMPOSITE_OP_SIZE 32
#define PAINTOP_ID_SIZE 40
#define SORT_PREFIX_LENGTH 3 // "b) "

// Defaults and limits when mapping a preset onto a Brush. Most Brush values are percentages.
#define DEFAULT_TIP_DIAMETER 20
#define DEFAULT_SPACING 0.1f
#define DEFAULT_TEXTURE_STRENGTH 0.6f
#define MAX_ANGLE_RADIANS 6.3f // a little over a full turn
#define HALF_TURN_DEGREES 180
#define IMAGE_TIP_HARDNESS 70
#define IMAGE_TIP_TEXTURE_STRENGTH 30 // an image tip keeps some of its look as noise
#define MIN_TEXTURE_PERCENT 10
#define AUTO_SPACING_PERCENT 10
#define SCATTER_PERCENT_PER_UNIT 50
// Krita's colour rate mostly follows pressure; the engine's paint amount is fixed, so it takes the middle.
#define COLOR_RATE_SHARE 0.5f

#define PATH_SIZE 300

static const u8 PNG_SIGNATURE[] = {0x89, 'P', 'N', 'G'};
#define KPP_EXTENSION ".kpp"
#define BUNDLE_EXTENSION ".bundle"

// The samples of a straight curve.
static const u8 LINEAR_CURVE[BRUSH_CURVE_POINTS] = {0, 64, 128, 192, 255};

typedef struct {
	const char *escaped;
	char character;
} XmlEntity;

static const XmlEntity XML_ENTITIES[] = {
        {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&amp;", '&'},
};
#define XML_ENTITY_COUNT (sizeof XML_ENTITIES / sizeof XML_ENTITIES[0])

// The auto-brush tip of a preset, as Krita stores it.
typedef struct {
	float diameter;
	float ratio;
	float horizontalFade;
	float angle; // radians
	float spacing; // fraction of the diameter
	float autoSpacing; // 1 when Krita picks the spacing
	bool square;
	bool autoBrush; // false for an image tip
} TipDefinition;

// Rounds a non-negative value to the nearest integer.
static int round_to_int(float value)
{
	return (int)(value + 0.5f);
}

static int to_percent(float fraction)
{
	return round_to_int(fraction * 100);
}

// The whole file plus a NUL, so text files can be read as strings.
static u8 *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	u8 *data = NULL;
	if (length > 0 && length < MAX_READ_FILE_BYTES) {
		data = malloc((size_t)length + 1);
	}
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	if (data) {
		data[length] = 0;
		*size = (size_t)length;
	}
	return data;
}

// The XML entity that text starts with, or NULL.
static const XmlEntity *entity_at(const char *text)
{
	for (unsigned i = 0; i < XML_ENTITY_COUNT; i++) {
		const char *escaped = XML_ENTITIES[i].escaped;
		if (!strncmp(text, escaped, strlen(escaped))) {
			return &XML_ENTITIES[i];
		}
	}
	return NULL;
}

// Replaces XML entities such as &lt; with their characters, in place.
static void unescape_xml(char *text)
{
	char *write = text;
	const char *read = text;
	while (*read) {
		const XmlEntity *entity = entity_at(read);
		if (entity) {
			*write = entity->character;
			read += strlen(entity->escaped);
		} else {
			*write = *read;
			read++;
		}
		write++;
	}
	*write = 0;
}

// Finds the value of <param name="NAME">value</param>, which may be a CDATA section, still escaped.
static bool find_param(const char *xml, const char *name, const char **value, size_t *length)
{
	char key[PARAM_KEY_SIZE];
	snprintf(key, sizeof key, "name=\"%s\"", name);
	const char *nameAttribute = strstr(xml, key);
	if (!nameAttribute) {
		return false;
	}
	const char *tagEnd = strchr(nameAttribute, '>');
	if (!tagEnd) {
		return false;
	}
	// An empty element: <param name="..."/>
	if (tagEnd[-1] == '/') {
		return false;
	}
	const char *valueStart = tagEnd + 1;
	const char *valueEnd;
	if (!strncmp(valueStart, CDATA_START, strlen(CDATA_START))) {
		valueStart += strlen(CDATA_START);
		valueEnd = strstr(valueStart, CDATA_END);
	} else {
		valueEnd = strstr(valueStart, PARAM_END);
	}
	if (!valueEnd) {
		return false;
	}
	*value = valueStart;
	*length = (size_t)(valueEnd - valueStart);
	return true;
}

// Reads the value of <param name="NAME">value</param>, unescaped. A longer value is cut to fit.
static bool read_param(const char *xml, const char *name, char *value, size_t valueSize)
{
	const char *valueStart;
	size_t length;
	if (!find_param(xml, name, &valueStart, &length)) {
		return false;
	}
	if (length >= valueSize) {
		length = valueSize - 1;
	}
	memcpy(value, valueStart, length);
	value[length] = 0;
	unescape_xml(value);
	return true;
}

// Reads the number of the first name="..." attribute in xml.
static bool read_attribute(const char *xml, const char *name, float *value)
{
	char key[ATTRIBUTE_KEY_SIZE];
	snprintf(key, sizeof key, " %s=\"", name);
	const char *found = strstr(xml, key);
	if (!found) {
		return false;
	}
	*value = strtof(found + strlen(key), NULL);
	return true;
}

static float read_float_param(const char *xml, const char *name, float fallback)
{
	char text[NUMBER_TEXT_SIZE];
	return read_param(xml, name, text, sizeof text) ? strtof(text, NULL) : fallback;
}

static bool read_bool_param(const char *xml, const char *name)
{
	char text[BOOL_TEXT_SIZE];
	return read_param(xml, name, text, sizeof text) && !strcmp(text, "true");
}

// Parses Krita's curve text "x,y;x,y;..." into up to CURVE_MAX_POINTS points. Returns how many it read.
static int parse_curve_points(const char *text, float *pointX, float *pointY)
{
	int count = 0;
	while (*text && count < CURVE_MAX_POINTS) {
		char *numberEnd;
		float x = strtof(text, &numberEnd);
		if (numberEnd == text || *numberEnd != ',') {
			break;
		}
		float y = strtof(numberEnd + 1, &numberEnd);
		pointX[count] = x;
		pointY[count] = y;
		count++;
		text = numberEnd;
		while (*text == ';' || *text == ' ') {
			text++;
		}
	}
	return count;
}

// The curve's value at x: linear between points, flat beyond the ends.
static float curve_value_at(const float *pointX, const float *pointY, int count, float x)
{
	if (x <= pointX[0]) {
		return pointY[0];
	}
	for (int i = 0; i + 1 < count; i++) {
		if (x <= pointX[i + 1]) {
			float width = pointX[i + 1] - pointX[i];
			if (width > 0) {
				return pointY[i] + (pointY[i + 1] - pointY[i]) * (x - pointX[i]) / width;
			}
			return pointY[i + 1];
		}
	}
	return pointY[count - 1];
}

// Samples Krita's curve (points over 0..1) into 0..255. A straight line stays all zero, the engine's
// default.
static void sample_curve(const char *text, u8 samples[BRUSH_CURVE_POINTS])
{
	float pointX[CURVE_MAX_POINTS];
	float pointY[CURVE_MAX_POINTS];
	int count = parse_curve_points(text, pointX, pointY);
	if (count < 2) {
		return;
	}
	bool straight = true;
	for (int k = 0; k < BRUSH_CURVE_POINTS; k++) {
		float y = curve_value_at(pointX, pointY, count, k * CURVE_SAMPLE_STEP);
		int value = clamp_int(round_to_int(y * 255), 0, 255);
		samples[k] = (u8)value;
		if (abs(value - LINEAR_CURVE[k]) > CURVE_STRAIGHT_TOLERANCE) {
			straight = false;
		}
	}
	if (straight) {
		memset(samples, 0, BRUSH_CURVE_POINTS);
	}
}

// The SENSOR_* flags that drive option ("Size", "Opacity" or "Flow"), and its response curve. A fade
// sensor also sets fadeLength.
static u8 read_sensors(const char *xml, const char *option, u8 curve[BRUSH_CURVE_POINTS], float *fadeLength)
{
	char key[SENSOR_PARAM_NAME_SIZE];
	char *sensorXml = malloc(SENSOR_XML_SIZE);
	// Krita's "Pressure<option>" param says whether the option is on at all.
	snprintf(key, sizeof key, "Pressure%s", option);
	if (!sensorXml || !read_bool_param(xml, key)) {
		free(sensorXml);
		return 0;
	}
	snprintf(key, sizeof key, "%sSensor", option);
	if (!read_param(xml, key, sensorXml, SENSOR_XML_SIZE)) {
		free(sensorXml);
		return SENSOR_PRESSURE;
	}
	u8 sensorFlags = 0;
	if (strstr(sensorXml, "id=\"pressure\"")) {
		sensorFlags |= SENSOR_PRESSURE;
	}
	if (strstr(sensorXml, "id=\"speed\"")) {
		sensorFlags |= SENSOR_SPEED;
	}
	const char *fadeSensor = strstr(sensorXml, "id=\"fade\"");
	if (fadeSensor) {
		sensorFlags |= SENSOR_FADE;
		float length;
		if (read_attribute(fadeSensor, "length", &length)) {
			*fadeLength = length;
		}
	}
	const char *curveTag = strstr(sensorXml, CURVE_TAG);
	if (curveTag) {
		sample_curve(curveTag + strlen(CURVE_TAG), curve);
	}
	free(sensorXml);
	// Pressure is the fallback when no supported sensor is listed.
	return sensorFlags ? sensorFlags : SENSOR_PRESSURE;
}

// Copies the quoted value that follows attributeStart (such as `name="`) in xml; empty when it is missing.
static void read_quoted_attribute(const char *xml, const char *attributeStart, char *value, size_t valueSize)
{
	const char *start = strstr(xml, attributeStart);
	if (!start) {
		value[0] = 0;
		return;
	}
	start += strlen(attributeStart);
	size_t length = strcspn(start, "\"");
	if (length > valueSize - 1) {
		length = valueSize - 1;
	}
	memcpy(value, start, length);
	value[length] = 0;
}

// Krita names often start with a sort prefix such as "b) ".
static const char *skip_sort_prefix(const char *name)
{
	bool hasPrefix = name[0] && name[1] == ')' && (name[2] == ' ' || name[2] == '_');
	return hasPrefix ? name + SORT_PREFIX_LENGTH : name;
}

// The preset's name, or fallbackName, without a sort prefix and with spaces for underscores.
static void read_brush_name(const char *xml, const char *fallbackName, char *brushName, size_t brushNameSize)
{
	char presetName[PRESET_NAME_SIZE] = "";
	const char *presetElement = strstr(xml, "<Preset");
	if (presetElement) {
		read_quoted_attribute(presetElement, " name=\"", presetName, sizeof presetName);
		unescape_xml(presetName);
	}
	if (!presetName[0]) {
		snprintf(presetName, sizeof presetName, "%s", fallbackName);
	}
	const char *shownName = skip_sort_prefix(presetName);
	snprintf(brushName, brushNameSize, "%.*s", (int)brushNameSize - 1, shownName);
	for (char *letter = brushName; *letter; letter++) {
		if (*letter == '_') {
			*letter = ' ';
		}
	}
}

// Reads the brush_definition param. Returns false only when out of memory.
static bool read_tip_definition(const char *xml, TipDefinition *tip)
{
	char *definition = malloc(TIP_DEFINITION_SIZE);
	if (!definition) {
		return false;
	}
	tip->diameter = DEFAULT_TIP_DIAMETER;
	tip->ratio = 1;
	tip->horizontalFade = 1;
	tip->angle = 0;
	tip->spacing = DEFAULT_SPACING;
	tip->autoSpacing = 0;
	tip->square = false;
	tip->autoBrush = true;
	if (read_param(xml, "brush_definition", definition, TIP_DEFINITION_SIZE)) {
		read_attribute(definition, "diameter", &tip->diameter);
		read_attribute(definition, "ratio", &tip->ratio);
		read_attribute(definition, "hfade", &tip->horizontalFade);
		read_attribute(definition, "spacing", &tip->spacing);
		read_attribute(definition, "useAutoSpacing", &tip->autoSpacing);
		const char *brushElement = strstr(definition, "<Brush");
		if (brushElement) {
			read_attribute(brushElement, "angle", &tip->angle);
		}
		const char *maskGenerator = strstr(definition, "<MaskGenerator");
		if (maskGenerator && strstr(maskGenerator, "type=\"rect\"")) {
			tip->square = true;
		}
		if (!strstr(definition, "auto_brush")) {
			tip->autoBrush = false;
		}
	}
	free(definition);
	// Some Krita versions store degrees, others radians.
	if (tip->angle > MAX_ANGLE_RADIANS) {
		tip->angle *= DEGREES_TO_RADIANS_ROUNDED;
	}
	return true;
}

static void apply_tip(Brush *brush, const TipDefinition *tip)
{
	brush->tip = tip->square ? TIP_SQUARE : TIP_ROUND;
	int hardness = tip->autoBrush ? to_percent(tip->horizontalFade) : IMAGE_TIP_HARDNESS;
	brush->hardness = (u8)clamp_int(hardness, 0, 100);
	brush->ratio = (u8)clamp_int(to_percent(tip->ratio), BRUSH_RATIO_MIN, 100);
	int angleDegrees = round_to_int(tip->angle * RADIANS_TO_DEGREES_ROUNDED) % HALF_TURN_DEGREES;
	brush->angle = (u8)clamp_int(angleDegrees, 0, BRUSH_ANGLE_MAX);
	int spacing = tip->autoSpacing > 0.5f ? AUTO_SPACING_PERCENT : to_percent(tip->spacing);
	brush->spacing = (u8)clamp_int(spacing, 1, 100);
}

// Whether Krita's rotation sensor turns the tip with the drawing angle.
static bool rotation_follows_stroke(const char *xml)
{
	char rotationSensor[ROTATION_SENSOR_XML_SIZE];
	return read_bool_param(xml, "PressureRotation") &&
	       read_param(xml, "RotationSensor", rotationSensor, sizeof rotationSensor) &&
	       strstr(rotationSensor, "drawingangle");
}

// Base64 text decoded into a new buffer. NULL when out of memory.
static u8 *decode_base64(const char *text, size_t length, size_t *size)
{
	u8 *bytes = malloc(base64_decoded_size(length));
	if (bytes) {
		*size = base64_decode(text, length, bytes);
	}
	return bytes;
}

// Stores the preset's texture with Pro Paint's textures. Krita keeps the PNG inside the preset as base64,
// mostly encoded twice.
static bool store_texture(const char *xml, const char *name)
{
	const char *encoded;
	size_t length;
	if (!find_param(xml, "Texture/Pattern/Pattern", &encoded, &length)) {
		return false;
	}
	size_t size = 0;
	u8 *decoded = decode_base64(encoded, length, &size);
	if (decoded && (size < sizeof PNG_SIGNATURE || memcmp(decoded, PNG_SIGNATURE, sizeof PNG_SIGNATURE))) {
		u8 *once = decoded;
		decoded = decode_base64((const char *)once, size, &size);
		free(once);
	}
	bool stored = decoded && textures_add_png(name, decoded, size);
	free(decoded);
	return stored;
}

static void apply_texture(Brush *brush, const char *xml)
{
	if (!read_bool_param(xml, "Texture/Pattern/Enabled")) {
		return;
	}
	float strength = read_float_param(xml, "Texture/Pattern/Strength", DEFAULT_TEXTURE_STRENGTH);
	brush->textureStrength = (u8)clamp_int(to_percent(strength), MIN_TEXTURE_PERCENT, 100);
	float scale = read_float_param(xml, "Texture/Pattern/Scale", 1);
	brush->textureScale = (u8)clamp_int(to_percent(scale), BRUSH_TEXTURE_SCALE_MIN, BRUSH_TEXTURE_SCALE_MAX);
	char name[BRUSH_TEXTURE_NAME_SIZE];
	if (!read_param(xml, "Texture/Pattern/Name", name, sizeof name)) {
		return;
	}
	// Without its picture the brush keeps the noise.
	if (textures_find(name) >= 0 || store_texture(xml, name)) {
		snprintf(brush->texture, sizeof brush->texture, "%s", name);
	}
}

static void apply_dab_settings(Brush *brush, const char *xml, bool autoBrush)
{
	brush->opacity = (u8)clamp_int(to_percent(read_float_param(xml, "OpacityValue", 1)), 1, 100);
	brush->flow = (u8)clamp_int(to_percent(read_float_param(xml, "FlowValue", 1)), 1, 100);
	if (read_bool_param(xml, "PressureScatter")) {
		float scatter = read_float_param(xml, "ScatterValue", 1);
		brush->scatter = (u8)clamp_int(round_to_int(scatter * SCATTER_PERCENT_PER_UNIT), 0, 100);
	}
	apply_texture(brush, xml);
	if (!autoBrush && !brush->textureStrength) {
		brush->textureStrength = IMAGE_TIP_TEXTURE_STRENGTH;
	}
	if (rotation_follows_stroke(xml)) {
		brush->follow = 1;
	}
}

// spacing is the tip's spacing as a fraction of its diameter.
static void apply_sensors(Brush *brush, const char *xml, float spacing)
{
	float fadeLength = 0;
	brush->sizeSensors = read_sensors(xml, "Size", brush->sizeCurve, &fadeLength);
	brush->opacitySensors = read_sensors(xml, "Opacity", brush->opacityCurve, &fadeLength);
	brush->flowSensors = read_sensors(xml, "Flow", brush->flowCurve, &fadeLength);
	// Krita's curve already says how small a light touch gets.
	brush->sizeMin = 0;
	brush->opacityMin = 0;
	if (fadeLength > 0) {
		// Krita counts the fade in dabs, the engine in dab diameters.
		brush->fade = (u8)clamp_int(round_to_int(fadeLength * spacing), 1, BRUSH_FADE_MAX);
	} else if ((brush->sizeSensors | brush->opacitySensors | brush->flowSensors) & SENSOR_FADE) {
		brush->fade = BRUSH_FADE_DEFAULT;
	}
}

// Krita's "erase" blending makes an eraser preset. Any brush erases with the eraser, so these are left out.
static bool is_eraser_preset(const char *xml)
{
	char compositeOp[COMPOSITE_OP_SIZE];
	return read_param(xml, "CompositeOp", compositeOp, sizeof compositeOp) && !strcmp(compositeOp, "erase");
}

static bool is_color_smudge(const char *xml)
{
	char paintopId[PAINTOP_ID_SIZE];
	read_quoted_attribute(xml, "paintopid=\"", paintopId, sizeof paintopId);
	return !strcmp(paintopId, "colorsmudge");
}

// Krita's color smudge engine smudges, and with its colour rate on it also paints (wet paint). Every other
// paint engine paints.
static void apply_mode(LibraryBrush *entry, const char *xml)
{
	if (!is_color_smudge(xml)) {
		entry->mode = BRUSH_MODE_PAINT;
		return;
	}
	entry->mode = BRUSH_MODE_SMUDGE;
	if (read_bool_param(xml, "PressureColorRate")) {
		float colorRate = read_float_param(xml, "ColorRateValue", 1) * COLOR_RATE_SHARE;
		entry->brush.paint = (u8)clamp_int(to_percent(colorRate), 0, 100);
	}
}

// The engines this app can follow: Krita's pixel brush, quick brush and color smudge. Presets of the others
// (bristles, spray, sketch, deform and so on) would only paint round dabs under their name.
static bool engine_supported(const char *xml)
{
	char paintopId[PAINTOP_ID_SIZE];
	read_quoted_attribute(xml, "paintopid=\"", paintopId, sizeof paintopId);
	return !paintopId[0] || !strcmp(paintopId, "paintbrush") || !strcmp(paintopId, "roundmarker") ||
	       !strcmp(paintopId, "colorsmudge");
}

// Adds one preset's XML to the library. Returns 1 when added.
static int import_xml(const char *xml, const char *fallbackName)
{
	if (!engine_supported(xml) || is_eraser_preset(xml)) {
		return 0;
	}
	LibraryBrush entry;
	memset(&entry, 0, sizeof entry);
	Brush *brush = &entry.brush;
	read_brush_name(xml, fallbackName, brush->name, sizeof brush->name);
	if (brush_library_find(brush->name) >= 0) {
		return 0;
	}
	TipDefinition tip;
	if (!read_tip_definition(xml, &tip)) {
		return 0;
	}
	apply_tip(brush, &tip);
	apply_dab_settings(brush, xml, tip.autoBrush);
	apply_sensors(brush, xml, tip.spacing);
	apply_mode(&entry, xml);
	entry.size = (u8)clamp_int(round_to_int(tip.diameter / 2), BRUSH_RADIUS_MIN, BRUSH_RADIUS_MAX);
	entry.fromKrita = 1;
	return brush_library_add(&entry) >= 0;
}

static int import_kpp(const u8 *file, size_t fileSize, const char *fallbackName)
{
	char *xml = kpp_file_preset_xml(file, fileSize);
	if (!xml) {
		return 0;
	}
	int added = import_xml(xml, fallbackName);
	free(xml);
	return added;
}

// Imports one entry of a bundle when it is a .kpp preset. Returns how many brushes it added.
static int import_zip_entry(ZipReader *zip, const ZipReaderEntry *entry)
{
	size_t extensionLength = strlen(KPP_EXTENSION);
	bool isPreset = entry->nameLength >= extensionLength &&
	                !strcmp(entry->name + entry->nameLength - extensionLength, KPP_EXTENSION);
	if (!isPreset) {
		return 0;
	}
	if (entry->compressedSize > MAX_COMPRESSED_PRESET_BYTES || entry->uncompressedSize > MAX_PRESET_BYTES) {
		return 0;
	}
	size_t presetSize = 0;
	u8 *preset = zip_reader_read(zip, entry, &presetSize);
	if (!preset) {
		return 0;
	}
	const char *slash = strrchr(entry->name, '/');
	const char *baseName = slash ? slash + 1 : entry->name;
	int added = import_kpp(preset, presetSize, baseName);
	free(preset);
	return added;
}

// Imports every .kpp in a bundle (a zip).
static int import_bundle(FILE *file)
{
	ZipReader zip;
	if (!zip_reader_open(&zip, file)) {
		return 0;
	}
	int added = 0;
	ZipReaderEntry entry;
	while (g_libraryCount < BRUSH_LIBRARY_MAX && zip_reader_next(&zip, &entry)) {
		added += import_zip_entry(&zip, &entry);
	}
	zip_reader_close(&zip);
	return added;
}

static int import_bundle_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return 0;
	}
	int added = import_bundle(file);
	fclose(file);
	return added;
}

// Whether name ends in extension, ignoring case, with at least one character before it.
static bool has_extension(const char *name, size_t nameLength, const char *extension)
{
	size_t extensionLength = strlen(extension);
	return nameLength > extensionLength && !strcasecmp(name + nameLength - extensionLength, extension);
}

// Whether line is one whole line of list (the first place it appears).
static bool list_has_line(const char *list, const char *line, size_t lineLength)
{
	const char *found = strstr(list, line);
	if (!found) {
		return false;
	}
	if (found != list && found[-1] != '\n') {
		return false;
	}
	char after = found[lineLength];
	return after == '\n' || after == '\r' || after == 0;
}

// Imports one .kpp file. It becomes a favorite when favorites lists its file name.
static int import_kpp_file(const char *path, const char *fileName, size_t nameLength, const char *favorites)
{
	size_t fileSize;
	u8 *file = read_file(path, &fileSize);
	if (!file) {
		return 0;
	}
	char baseName[PRESET_NAME_SIZE];
	snprintf(baseName, sizeof baseName, "%.*s", (int)(nameLength - strlen(KPP_EXTENSION)), fileName);
	int newIndex = g_libraryCount;
	int added = import_kpp(file, fileSize, baseName);
	if (added && favorites && list_has_line(favorites, fileName, nameLength)) {
		g_library[newIndex].favorite = 1;
	}
	free(file);
	return added;
}

// favorites: file names, one per line, that go on the pop-up palette, or NULL. Returns -1 when the folder
// cannot be opened.
static int import_directory(const char *directoryPath, const char *favorites, int *fileCount)
{
	DIR *directory = opendir(directoryPath);
	if (!directory) {
		return -1;
	}
	int added = 0;
	struct dirent *entry;
	while ((entry = readdir(directory)) && g_libraryCount < BRUSH_LIBRARY_MAX) {
		const char *fileName = entry->d_name;
		size_t fileNameLength = strlen(fileName);
		bool isPreset = has_extension(fileName, fileNameLength, KPP_EXTENSION);
		bool isBundle = has_extension(fileName, fileNameLength, BUNDLE_EXTENSION);
		if (!isPreset && !isBundle) {
			continue;
		}
		char path[PATH_SIZE];
		snprintf(path, sizeof path, "%s/%s", directoryPath, fileName);
		(*fileCount)++;
		if (isBundle) {
			added += import_bundle_file(path);
		} else {
			added += import_kpp_file(path, fileName, fileNameLength, favorites);
		}
	}
	closedir(directory);
	return added;
}

int krita_import_directory(const char *folder, const char *favoritesPath, int *fileCount)
{
	*fileCount = 0;
	size_t favoritesSize = 0;
	u8 *favorites = favoritesPath ? read_file(favoritesPath, &favoritesSize) : NULL;
	int added = import_directory(folder, (const char *)favorites, fileCount);
	free(favorites);
	return added;
}

int krita_import_folder(char *message, size_t messageSize)
{
	int fileCount = 0;
	int added = krita_import_directory(BRUSH_IMPORT_DIR, NULL, &fileCount);
	if (added < 0) {
		snprintf(message, messageSize, "Put brushes in " BRUSH_IMPORT_FOLDER);
		return 0;
	}
	if (!fileCount) {
		snprintf(message, messageSize, "No brushes in " BRUSH_IMPORT_FOLDER);
		return added;
	}
	const char *brushPlural = added == 1 ? "" : "es";
	if (g_libraryCount >= BRUSH_LIBRARY_MAX) {
		snprintf(message, messageSize, "%d new brush%s, library full", added, brushPlural);
	} else {
		const char *filePlural = fileCount == 1 ? "" : "s";
		snprintf(message, messageSize, "%d new brush%s from %d file%s", added, brushPlural, fileCount,
		         filePlural);
	}
	return added;
}
