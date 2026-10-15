#include "pressure.h"
#include "app_state.h"
#include "tpressure.h"

#define CIRCLE_PAD_WAIT_FRAMES 12 // after a pressure stroke: HID's Circle Pad values lag the switch back

// The first pressure readings of a touch can be off while the chip switches its report layout and the
// stylus settles. So each frame's reading goes through a median of the last three and a step limit, and a
// stroke waits for the median to fill, or for PRESSURE_WARMUP_FRAMES, before it is laid down.
#define PRESSURE_MEDIAN_SIZE 3
#define PRESSURE_WARMUP_FRAMES 12
#define PRESSURE_MAX_STEP 0.12f // most the pressure may move in one frame
#define PRESSURE_BEFORE_READING 0.35f // a moderate pressure: "unknown" would count as full and spike

#define CITRA_SYSTEM_INFO_TYPE 0x20000 // Citra answers this GetSystemInfo type; a real 3DS does not
#define LUMA_SYSTEM_INFO_TYPE 0x10000 // Luma3DS answers this GetSystemInfo type with its version

bool g_pressureWanted = true;

static bool g_pressureOpen; // tpressure opened
static int g_circlePadWait; // frames the Circle Pad is not read (see pressure_circle_pad_usable)
static float g_recentPressures[PRESSURE_MEDIAN_SIZE];
static int g_validReadings; // valid pressure readings since the stroke began
static int g_pressureFrames; // frames since the stroke began
static float g_filteredPressure = -1; // -1: no valid reading yet

static void reset_filter(void)
{
	g_validReadings = 0;
	g_filteredPressure = -1;
	g_pressureFrames = 0;
}

static float median_of_three(float a, float b, float c)
{
	if (a > b) {
		if (b > c) {
			return b;
		}
		return a > c ? c : a;
	}
	if (a > c) {
		return a;
	}
	return b > c ? c : b;
}

void pressure_read(void)
{
	if (!g_pressureOpen) {
		return;
	}
	g_pressureFrames++;
	float reading = tp_pressure();
	if (reading < 0) {
		return;
	}
	g_recentPressures[g_validReadings % PRESSURE_MEDIAN_SIZE] = reading;
	g_validReadings++;
	float smoothed = reading;
	if (g_validReadings >= PRESSURE_MEDIAN_SIZE) {
		smoothed = median_of_three(g_recentPressures[0], g_recentPressures[1], g_recentPressures[2]);
	} else if (g_validReadings == 2) {
		smoothed = (g_recentPressures[0] + g_recentPressures[1]) * 0.5f;
	}
	if (g_filteredPressure < 0) {
		g_filteredPressure = smoothed;
		return;
	}
	float change = smoothed - g_filteredPressure;
	if (change > PRESSURE_MAX_STEP) {
		change = PRESSURE_MAX_STEP;
	} else if (change < -PRESSURE_MAX_STEP) {
		change = -PRESSURE_MAX_STEP;
	}
	g_filteredPressure += change;
}

float pressure_current(void)
{
	if (!g_pressureOpen) {
		return PRESSURE_NONE;
	}
	return g_filteredPressure < 0 ? PRESSURE_BEFORE_READING : g_filteredPressure;
}

bool pressure_settled(void)
{
	return !g_pressureOpen || g_validReadings >= PRESSURE_MEDIAN_SIZE ||
	       g_pressureFrames > PRESSURE_WARMUP_FRAMES;
}

// The chip reports pressure only during strokes.
void pressure_stroke_begin(void)
{
	if (g_pressureOpen) {
		tp_pen(true);
		tp_sample();
	}
	reset_filter();
	pressure_read();
}

void pressure_stroke_end(void)
{
	if (g_pressureOpen) {
		tp_pen(false);
		g_circlePadWait = CIRCLE_PAD_WAIT_FRAMES;
	}
}

static bool running_in_emulator(void)
{
	s64 answer = 0;
	Result result = svcGetSystemInfo(&answer, CITRA_SYSTEM_INFO_TYPE, 0);
	return R_SUCCEEDED(result) && answer;
}

static bool luma_installed(void)
{
	s64 version = 0;
	Result result = svcGetSystemInfo(&version, LUMA_SYSTEM_INFO_TYPE, 0);
	return R_SUCCEEDED(result) && version;
}

// Pressure needs the touch chip's own session (tpressure), opened only when asked for.
void pressure_set_wanted(bool wanted)
{
	if (wanted && running_in_emulator()) {
		app_toast("No pressure in an emulator");
		wanted = false;
	}
	if (wanted && !g_pressureOpen) {
		g_pressureOpen = tp_open(NULL, 0);
		if (!g_pressureOpen) {
			// With Luma3DS, tp_open fails only when it gets no session of the touch chip's service.
			app_toast(luma_installed() ? "No pressure: touch chip busy" : "No pressure: needs Luma3DS");
			wanted = false;
		}
	}
	if (!wanted && g_pressureOpen) {
		tp_close();
		g_pressureOpen = false;
	}
	g_pressureWanted = wanted;
	app_settings_changed();
}

// Unlike pressure_set_wanted, a failed open here keeps the wish, so the next start tries again.
void pressure_open(void)
{
	if (g_pressureWanted && !running_in_emulator()) {
		g_pressureOpen = tp_open(NULL, 0);
	}
}

void pressure_close(void)
{
	if (g_pressureOpen) {
		tp_close();
	}
}

void pressure_pause(void)
{
	if (g_pressureOpen) {
		tp_pen(false);
	}
}

void pressure_poll(void)
{
	if (!g_pressureOpen) {
		return;
	}
	if (!g_penDown) {
		tp_pen(false);
		return;
	}
	tp_sample();
	if (tp_stalled()) {
		// The touch chip stopped scanning with pressure on. Drawing goes on without it.
		pressure_set_wanted(false);
		app_toast("Pressure pen does not work on this 3DS");
	}
}

// During a pressure stroke the touch report carries the pressure in place of the Circle Pad, and HID's
// Circle Pad values lag the switch back.
bool pressure_circle_pad_usable(bool touchHeld)
{
	if (touchHeld) {
		if (g_circlePadWait < 1) {
			g_circlePadWait = 1;
		}
	} else if (g_circlePadWait > 0) {
		g_circlePadWait--;
	}
	return !g_circlePadWait;
}
