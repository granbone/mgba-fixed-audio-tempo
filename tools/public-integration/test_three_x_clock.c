/* SPDX-License-Identifier: MPL-2.0 */
#include <mgba/core/audio-clock.h>
#include "fixed_audio_rate.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
	const double fps = 16777216.0 / 280896.0;
	assert(fixedAudioResolveMode("disabled", NULL, "1", "1") == FIXED_AUDIO_DISABLED);
	assert(fixedAudioResolveMode("experimental", "OFF", "1", "1") == FIXED_AUDIO_DISABLED);
	assert(fixedAudioResolveMode("experimental", NULL, "0", "1") == FIXED_AUDIO_DISABLED);
	assert(fixedAudioResolveMode("conservative", NULL, "1", "1") == FIXED_AUDIO_CONSERVATIVE);
	for (unsigned mode = 0; mode < 3; ++mode) {
		for (unsigned speed = 1; speed <= 4; ++speed) {
			unsigned expected = mode == FIXED_AUDIO_DISABLED || speed == 4 ||
				(speed == 3 && mode != FIXED_AUDIO_EXPERIMENTAL) ? 0 : speed;
			assert(fixedAudioSupportedSpeed(mode, true, speed == 1, speed != 1, fps, fps*speed) == expected);
		}
	}
	const double invalid[] = {0, -1, NAN, INFINITY, 37, fps*2.5, fps*3.03};
	for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i)
		assert(!fixedAudioSupportedSpeed(FIXED_AUDIO_EXPERIMENTAL, true, false, true, fps, invalid[i]));
	assert(!fixedAudioSupportedSpeed(FIXED_AUDIO_EXPERIMENTAL, false, false, true, fps, fps*3));
	assert(!fixedAudioSupportedSpeed(FIXED_AUDIO_EXPERIMENTAL, true, false, false, fps, fps*3));
	assert(!fixedAudioSupportedSpeed(FIXED_AUDIO_EXPERIMENTAL, true, true, false, fps, fps*3));
	const unsigned patterns[][6] = {{1,2,3,3,2,1}, {1,3,1,3,1,3}, {3,1,3,1,3,1}};
	const double sampleRates[] = {32768, 48000, 65536};
	for (unsigned r = 0; r < 3; ++r) {
		for (unsigned pattern = 0; pattern < 3; ++pattern) {
			struct mAudioClock c;
			mAudioClockInit(&c, fps, sampleRates[r]);
			double desired = 0;
			uint64_t game = 0, previous = 0;
			for (unsigned part = 0; part < 6; ++part) {
				unsigned speed = patterns[pattern][part];
				mAudioClockSetFrontendRate(&c, true, fps*speed);
				for (unsigned i = 0; i < 180000; ++i) {
					uint32_t advance = mAudioClockBeginRun(&c, game, 280896);
					assert(c.runStartSample == previous && advance > 0);
					assert(mAudioClockTimestampForCycle(&c, game) == previous);
					assert(mAudioClockTimestampForCycle(&c, game+140448) == previous+advance/2);
					assert(mAudioClockTimestampForCycle(&c, game+280896) == previous+advance);
					mAudioClockEndRun(&c);
					desired += sampleRates[r]/(fps*speed);
					previous += advance; game += 280896;
				}
			}
			assert(fabs((double)c.absoluteAudioSample-desired) < 1.01);
			printf("sampleRate=%.0f pattern=%u runs=1080000 error=%.6f PASS\n", sampleRates[r], pattern, c.absoluteAudioSample-desired);
			mAudioClockSetFrontendRate(&c, true, 0);
			assert(!mAudioClockBeginRun(&c, game, 280896)); mAudioClockEndRun(&c);
			assert(c.absoluteAudioSample == previous);
		}
	}
	puts("mode/rate guards, event timestamps, switch continuity, fractional drift PASS");
	return 0;
}
