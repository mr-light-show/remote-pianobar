#pragma once

#include <stdlib.h>
#include <check.h>
#include "../src/player.h"

/* Only physical-device assertions are skipped by an explicit no-device engine. */
static void BarTestAssertDeviceStarted (player_t *player, bool started) {
	ck_assert (player->engineInitialized);
	ma_device *device = ma_engine_get_device (&player->engine);
	if (device == NULL) { ck_assert (player->audioNoDevice); return; }
	ck_assert_int_eq (ma_device_is_started (device) != MA_FALSE, started);
}

/* Test callback surrogate for the absent noDevice callback. The caller waits
 * for stable PLAYING and makes no test-driven lifecycle transition during
 * this single read. One second drains the hand-checked 0.500000-second fixture.
 * Once the read returns there is no further engine access; normal completion
 * and cleanup are observed through the player. No reservation fields are
 * inspected or changed, just as the real device callback does not reserve. */
static bool BarTestDrainAudioFixture (player_t *player) {
	if (!player->audioNoDevice) { return true; }
	const ma_uint32 channels = ma_engine_get_channels (&player->engine);
	const ma_uint32 rate = ma_engine_get_sample_rate (&player->engine);
	float *samples = calloc ((size_t)rate * channels, sizeof (*samples));
	if (samples == NULL) { return false; }
	const ma_result result = ma_engine_read_pcm_frames (&player->engine, samples, rate, NULL);
	free (samples);
	return result == MA_SUCCESS;
}
