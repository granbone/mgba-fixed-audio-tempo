/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/core/audio-clock.h>

#include <math.h>
#include <string.h>

void mAudioClockInit(struct mAudioClock* clock, double nominalRunRate, double outputSampleRate) {
	memset(clock, 0, sizeof(*clock));
	if (!isfinite(nominalRunRate) || nominalRunRate <= 0.0 ||
	    !isfinite(outputSampleRate) || outputSampleRate <= 0.0) {
		return;
	}
	clock->enabled = true;
	clock->nominalRunRate = nominalRunRate;
	clock->frontendRunRate = nominalRunRate;
	clock->speedRatio = 1.0;
	clock->outputSampleRate = outputSampleRate;
}

void mAudioClockSetFrontendRate(struct mAudioClock* clock, bool known, double frontendRunRate) {
	if (!clock->enabled) {
		return;
	}
	clock->frontendRateKnown = known;
	clock->unlimited = known && frontendRunRate == 0.0;
	if (!known) {
		clock->frontendRunRate = clock->nominalRunRate;
	} else {
		clock->frontendRunRate = frontendRunRate;
	}
	if (isfinite(clock->frontendRunRate) && clock->frontendRunRate > 0.0) {
		clock->speedRatio = clock->nominalRunRate / clock->frontendRunRate;
	} else {
		clock->speedRatio = 0.0;
	}
}

uint32_t mAudioClockBeginRun(struct mAudioClock* clock, uint64_t gameCycle, uint64_t cycleSpan) {
	if (!clock->enabled) {
		return 0;
	}
	clock->runStartSample = clock->absoluteAudioSample;
	clock->runStartGameCycle = gameCycle;
	clock->runCycleSpan = cycleSpan;
	clock->runAdvance = 0;
	clock->runActive = true;
	if (clock->paused || clock->unlimited || !isfinite(clock->frontendRunRate) ||
	    clock->frontendRunRate <= 0.0) {
		return 0;
	}
	double desired = clock->fractionalAccumulator + clock->outputSampleRate / clock->frontendRunRate;
	if (!isfinite(desired) || desired > UINT32_MAX) {
		return 0;
	}
	clock->runAdvance = (uint32_t) desired;
	clock->fractionalAccumulator = desired - clock->runAdvance;
	return clock->runAdvance;
}

uint64_t mAudioClockTimestampForCycle(const struct mAudioClock* clock, uint64_t gameCycle) {
	if (!clock->enabled) {
		return 0;
	}
	if (!clock->runActive || !clock->runCycleSpan || gameCycle <= clock->runStartGameCycle) {
		return clock->absoluteAudioSample;
	}
	uint64_t offset = gameCycle - clock->runStartGameCycle;
	if (offset >= clock->runCycleSpan) {
		return clock->runStartSample + clock->runAdvance;
	}
	return clock->runStartSample + (uint64_t) ((double) clock->runAdvance * offset / clock->runCycleSpan);
}

void mAudioClockEndRun(struct mAudioClock* clock) {
	if (!clock->enabled || !clock->runActive) {
		return;
	}
	clock->absoluteAudioSample += clock->runAdvance;
	clock->runActive = false;
}
