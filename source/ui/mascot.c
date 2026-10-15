// Cort's state machine. A reaction plays once, or for a hold time if it loops, then he idles again. Left
// alone he strolls and plays small animations. After a while without input he sleeps until the next input.
// Events from the app interrupt whatever he is doing.
#include <stdlib.h>
#include "cort.h"
#include "draw.h"
#include "framebuffer.h"
#include "mascot.h"

#define SLEEP_AFTER_MS 45000 // without input
#define WALK_SPEED 9.0f // sprite pixels per second
#define RUN_SPEED 26.0f
#define MIN_STROLL 16 // sprite pixels
#define SECONDS_PER_MS 0.001f
#define MAX_TICK_SECONDS 0.1f // a long frame does not make him jump
#define BLINK_GAP_MIN_MS 2000
#define BLINK_GAP_SPREAD_MS 4000
#define STROKES_PER_SWOON 40

// How long a looping animation holds, in ms.
#define HOLD_GREETING_MS 2200
#define HOLD_HELLO_MS 1800
#define HOLD_EXPORTED_MS 1600
#define HOLD_SAD_MS 2500
#define HOLD_REFUSED_MS 1500
#define HOLD_SWOON_MS 2400
#define HOLD_THINK_MIN_MS 1800
#define HOLD_THINK_MAX_MS 3200
#define HOLD_WAVE_MS 1500
#define HOLD_SHRUG_MS 1300
#define HOLD_POINT_MS 1800
#define HOLD_CHEER_MS 1100
#define HOLD_LOVE_MS 1800
#define GAME_ROUNDS 2

// How long he waits before he does something of his own, in ms: a random time from MIN to MAX.
#define WAIT_AT_START_MIN_MS 4000
#define WAIT_AT_START_MAX_MS 6000
#define WAIT_AFTER_EVENT_MIN_MS 3500
#define WAIT_AFTER_EVENT_MAX_MS 6000
#define WAIT_AFTER_WAKING_MIN_MS 2500
#define WAIT_AFTER_WAKING_MAX_MS 4000
#define WAIT_AFTER_STROLL_MIN_MS 1500
#define WAIT_AFTER_STROLL_MAX_MS 5000
#define WAIT_AFTER_ACTIVITY_MIN_MS 4500
#define WAIT_AFTER_ACTIVITY_MAX_MS 8000

#define NOTHING_SHOWN -1 // a look field before the first mascot_draw

// The palette colours are 0xAARRGGBB.
#define ARGB_ALPHA_SHIFT 24
#define ARGB_RED_SHIFT 16
#define ARGB_GREEN_SHIFT 8

enum {
	MOTION_NONE,
	MOTION_WALK,
	MOTION_RUN
};

// What he does of his own accord.
enum {
	ACTIVITY_RUN,
	ACTIVITY_WALK,
	ACTIVITY_THINK,
	ACTIVITY_GAME,
	ACTIVITY_WAVE,
	ACTIVITY_JUMP,
	ACTIVITY_SHRUG,
	ACTIVITY_POINT,
	ACTIVITY_CHEER,
	ACTIVITY_LOVE
};

// Each entry covers a slice of the roll (0..99) in proportion to its weight, in this order. RUN has a
// slice at both ends.
typedef struct {
	int activity;
	int weight;
} ActivityWeight;

static const ActivityWeight ACTIVITY_WEIGHTS[] = {
        {ACTIVITY_RUN, 6},   {ACTIVITY_WALK, 39}, {ACTIVITY_THINK, 11}, {ACTIVITY_GAME, 12},
        {ACTIVITY_WAVE, 6},  {ACTIVITY_JUMP, 6},  {ACTIVITY_SHRUG, 5},  {ACTIVITY_POINT, 5},
        {ACTIVITY_CHEER, 4}, {ACTIVITY_LOVE, 3},  {ACTIVITY_RUN, 3},
};
#define ACTIVITY_WEIGHT_COUNT ((int)(sizeof ACTIVITY_WEIGHTS / sizeof ACTIVITY_WEIGHTS[0]))

// What a mascot_draw showed, for mascot_changed.
enum {
	LOOK_ANIMATION,
	LOOK_FRAME,
	LOOK_MIRRORED,
	LOOK_OFFSET,
	LOOK_FIELD_COUNT
};

static int g_animation = ANIMATION_IDLE;
static int g_frame;
static int g_nextAnimation = -1; // plays after a one-shot animation; -1: idle
static int g_nextHoldMs;
static int g_strokeCount;
static u64 g_frameStart;
static u64 g_holdUntil; // a looping animation ends at this time; 0: it loops until something else plays
static u64 g_blinkAt;
static u64 g_lastInputAt;
static u64 g_lastTick;
static u64 g_nextActivityAt;
static int g_motion = MOTION_NONE;
static float g_distance; // from his home corner toward the other side, in sprite pixels
static float g_targetDistance;
static int g_wanderDirection = -1; // which way he wanders from home on screen: -1 left, 1 right
static bool g_mirrored; // the frames face left; mirrored, he faces right
static int g_shownLook[LOOK_FIELD_COUNT] = {
        [LOOK_ANIMATION] = NOTHING_SHOWN,
        [LOOK_FRAME] = NOTHING_SHOWN,
        [LOOK_MIRRORED] = NOTHING_SHOWN,
        [LOOK_OFFSET] = NOTHING_SHOWN,
};
static int g_shownScale = 1;

static int random_between(int min, int max)
{
	return min + rand() % (max - min + 1);
}

static void play(int animation, int holdMs)
{
	u64 now = osGetTime();
	g_animation = animation;
	g_frame = 0;
	g_frameStart = now;
	g_holdUntil = MASCOT_ANIMATIONS[animation].loops && holdMs ? now + holdMs : 0;
	g_nextAnimation = -1;
}

// Plays first once, then next for holdMs.
static void play_then(int first, int next, int holdMs)
{
	play(first, 0);
	g_nextAnimation = next;
	g_nextHoldMs = holdMs;
}

// He stops strolling and does something of his own again after minMs to maxMs.
static void stop_and_wait(u64 now, int minMs, int maxMs)
{
	g_motion = MOTION_NONE;
	g_nextActivityAt = now + random_between(minMs, maxMs);
}

void mascot_init(void)
{
	u64 now = osGetTime();
	g_lastTick = now;
	g_lastInputAt = now;
	srand((unsigned)now);
	play(ANIMATION_WAVE, HOLD_GREETING_MS);
	stop_and_wait(now, WAIT_AT_START_MIN_MS, WAIT_AT_START_MAX_MS);
}

void mascot_event(MascotEvent event)
{
	if (event != MASCOT_STROKE) {
		stop_and_wait(osGetTime(), WAIT_AFTER_EVENT_MIN_MS, WAIT_AFTER_EVENT_MAX_MS);
	}
	switch (event) {
	case MASCOT_HELLO:
		play(ANIMATION_WAVE, HOLD_HELLO_MS);
		break;
	case MASCOT_SAVED:
		play(ANIMATION_THUMBS_UP, 0);
		break;
	case MASCOT_EXPORTED:
		play(ANIMATION_CHEER, HOLD_EXPORTED_MS);
		break;
	case MASCOT_FAILED:
		play_then(ANIMATION_HURT, ANIMATION_SAD, HOLD_SAD_MS);
		break;
	case MASCOT_SAD:
		play(ANIMATION_SAD, HOLD_SAD_MS);
		break;
	case MASCOT_REFUSED:
		play(ANIMATION_SHRUG, HOLD_REFUSED_MS);
		break;
	case MASCOT_IDEA:
		play(ANIMATION_IDEA, 0);
		break;
	case MASCOT_NEW_DRAWING:
		play(ANIMATION_SURPRISED, 0);
		break;
	case MASCOT_STROKE:
		g_strokeCount++;
		if (g_strokeCount % STROKES_PER_SWOON == 0 && g_animation == ANIMATION_IDLE &&
		    g_motion == MOTION_NONE) {
			play(ANIMATION_LOVE, HOLD_SWOON_MS);
		}
		break;
	}
}

void mascot_input(void)
{
	u64 now = osGetTime();
	g_lastInputAt = now;
	if (g_animation == ANIMATION_SLEEP) {
		play(ANIMATION_SURPRISED, 0);
		stop_and_wait(now, WAIT_AFTER_WAKING_MIN_MS, WAIT_AFTER_WAKING_MAX_MS);
	}
}

// Walks or runs to a new spot at least MIN_STROLL away.
static void start_stroll(bool run)
{
	float target;
	do {
		target = (float)random_between(0, MASCOT_WANDER_RANGE);
	} while (target - g_distance < MIN_STROLL && g_distance - target < MIN_STROLL);
	g_targetDistance = target;
	g_motion = run ? MOTION_RUN : MOTION_WALK;
	play(run ? ANIMATION_RUN : ANIMATION_WALK, 0);
}

static int game_length_ms(void)
{
	const MascotAnimation *game = &MASCOT_ANIMATIONS[ANIMATION_PLAY3DS];
	int lengthMs = 0;
	for (int i = 0; i < game->frameCount; i++) {
		lengthMs += GAME_ROUNDS * game->frameMs[i];
	}
	return lengthMs;
}

static int pick_activity(void)
{
	int totalWeight = 0;
	for (int i = 0; i < ACTIVITY_WEIGHT_COUNT; i++) {
		totalWeight += ACTIVITY_WEIGHTS[i].weight;
	}
	int roll = random_between(0, totalWeight - 1);
	for (int i = 0; i < ACTIVITY_WEIGHT_COUNT; i++) {
		roll -= ACTIVITY_WEIGHTS[i].weight;
		if (roll < 0) {
			return ACTIVITY_WEIGHTS[i].activity;
		}
	}
	return ACTIVITY_WALK;
}

// Plays an activity that stays in place. Returns how long it runs past its usual wait, in ms.
static int play_activity(int activity)
{
	switch (activity) {
	case ACTIVITY_THINK:
		play(ANIMATION_THINK, random_between(HOLD_THINK_MIN_MS, HOLD_THINK_MAX_MS));
		break;
	case ACTIVITY_GAME: {
		// A game is long: the wait after it starts when it ends.
		int gameMs = game_length_ms();
		play(ANIMATION_PLAY3DS, gameMs);
		return gameMs;
	}
	case ACTIVITY_WAVE:
		play(ANIMATION_WAVE, HOLD_WAVE_MS);
		break;
	case ACTIVITY_JUMP:
		play(ANIMATION_JUMP, 0);
		break;
	case ACTIVITY_SHRUG:
		play(ANIMATION_SHRUG, HOLD_SHRUG_MS);
		break;
	case ACTIVITY_POINT:
		// He points at something on either side.
		g_mirrored = !g_mirrored;
		play(ANIMATION_POINT, HOLD_POINT_MS);
		break;
	case ACTIVITY_CHEER:
		play(ANIMATION_CHEER, HOLD_CHEER_MS);
		break;
	default:
		play(ANIMATION_LOVE, HOLD_LOVE_MS);
		break;
	}
	return 0;
}

static void choose_activity(u64 now)
{
	int activity = pick_activity();
	if (activity == ACTIVITY_RUN || activity == ACTIVITY_WALK) {
		start_stroll(activity == ACTIVITY_RUN);
		return;
	}
	int extraMs = play_activity(activity);
	stop_and_wait(now, WAIT_AFTER_ACTIVITY_MIN_MS + extraMs, WAIT_AFTER_ACTIVITY_MAX_MS + extraMs);
}

// Steps through the frames that are due. Returns true when the animation ended and another one started.
static bool advance_frames(u64 now)
{
	const MascotAnimation *animation = &MASCOT_ANIMATIONS[g_animation];
	while (now - g_frameStart >= animation->frameMs[g_frame]) {
		g_frameStart += animation->frameMs[g_frame];
		g_frame++;
		if (g_frame < animation->frameCount) {
			continue;
		}
		if (animation->loops) {
			g_frame = 0;
			continue;
		}
		if (g_nextAnimation >= 0) {
			play(g_nextAnimation, g_nextHoldMs);
		} else {
			play(ANIMATION_IDLE, 0);
		}
		return true;
	}
	return false;
}

// One step toward his target spot. There he stops and waits.
static void stroll(u64 now, float seconds)
{
	float speed = g_motion == MOTION_RUN ? RUN_SPEED : WALK_SPEED;
	float step = speed * seconds;
	float remaining = g_targetDistance - g_distance;
	// He faces the way he goes on screen.
	g_mirrored = g_wanderDirection * (remaining > 0 ? 1 : -1) > 0;
	if (remaining > step) {
		g_distance += step;
	} else if (remaining < -step) {
		g_distance -= step;
	} else {
		g_distance = g_targetDistance;
		play(ANIMATION_IDLE, 0);
		stop_and_wait(now, WAIT_AFTER_STROLL_MIN_MS, WAIT_AFTER_STROLL_MAX_MS);
	}
}

void mascot_tick(void)
{
	u64 now = osGetTime();
	float seconds = (now - g_lastTick) * SECONDS_PER_MS;
	g_lastTick = now;
	if (seconds > MAX_TICK_SECONDS) {
		seconds = MAX_TICK_SECONDS;
	}
	if (advance_frames(now)) {
		return;
	}
	if (g_holdUntil && now >= g_holdUntil) {
		play(ANIMATION_IDLE, 0);
		return;
	}
	if (g_animation == ANIMATION_SLEEP) {
		return;
	}
	if (g_motion != MOTION_NONE) {
		stroll(now, seconds);
		return;
	}
	if (g_animation != ANIMATION_IDLE) {
		return;
	}
	if (now - g_lastInputAt > SLEEP_AFTER_MS) {
		play(ANIMATION_SLEEP, 0);
		return;
	}
	if (now >= g_nextActivityAt) {
		choose_activity(now);
		return;
	}
	if (now >= g_blinkAt) {
		// No blink on the first idle tick, only the timer starts.
		if (g_blinkAt) {
			play(ANIMATION_BLINK, 0);
		}
		g_blinkAt = now + BLINK_GAP_MIN_MS + rand() % BLINK_GAP_SPREAD_MS;
	}
}

static int offset_pixels(int scale)
{
	return (int)(g_distance * scale + 0.5f);
}

static void get_look(int look[LOOK_FIELD_COUNT], int scale)
{
	look[LOOK_ANIMATION] = g_animation;
	look[LOOK_FRAME] = g_frame;
	look[LOOK_MIRRORED] = g_mirrored;
	look[LOOK_OFFSET] = offset_pixels(scale);
}

bool mascot_changed(void)
{
	int look[LOOK_FIELD_COUNT];
	get_look(look, g_shownScale);
	for (int i = 0; i < LOOK_FIELD_COUNT; i++) {
		if (look[i] != g_shownLook[i]) {
			return true;
		}
	}
	return false;
}

void mascot_draw(u8 *pixels, int width, int height, int homeX, int homeY, int scale)
{
	g_shownScale = scale;
	get_look(g_shownLook, scale);
	// His home is at one end of the image. He wanders toward the other.
	int spriteCenterX = homeX + MASCOT_FRAME_SIZE * scale / 2;
	g_wanderDirection = spriteCenterX > width / 2 ? -1 : 1;
	int left = homeX + g_wanderDirection * offset_pixels(scale);
	const u8 *frame = MASCOT_FRAMES[MASCOT_ANIMATIONS[g_animation].firstFrame + g_frame];
	for (int y = 0; y < MASCOT_FRAME_SIZE; y++) {
		for (int x = 0; x < MASCOT_FRAME_SIZE; x++) {
			int frameX = g_mirrored ? MASCOT_FRAME_SIZE - 1 - x : x;
			u8 colorIndex = frame[y * MASCOT_FRAME_SIZE + frameX];
			if (!colorIndex) {
				continue;
			}
			// Edge pixels carry partial alpha.
			u32 argb = MASCOT_PALETTE[colorIndex];
			int alpha = argb >> ARGB_ALPHA_SHIFT;
			int red = (argb >> ARGB_RED_SHIFT) & 255;
			int green = (argb >> ARGB_GREEN_SHIFT) & 255;
			int blue = argb & 255;
			// Each sprite pixel covers scale x scale image pixels.
			for (int blockX = 0; blockX < scale; blockX++) {
				int imageX = left + x * scale + blockX;
				if ((unsigned)imageX >= (unsigned)width) {
					continue;
				}
				for (int blockY = 0; blockY < scale; blockY++) {
					int imageY = homeY + y * scale + blockY;
					if ((unsigned)imageY >= (unsigned)height) {
						continue;
					}
					u8 *pixel = pixels +
					            ((size_t)imageX * height + (height - 1 - imageY)) * FRAMEBUFFER_PIXEL_BYTES;
					blend_bgr_pixel(pixel, red, green, blue, alpha);
				}
			}
		}
	}
}

int mascot_frame_size(void)
{
	return MASCOT_FRAME_SIZE;
}
