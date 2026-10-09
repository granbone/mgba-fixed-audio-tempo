/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef M_CORE_AUDIO_CLOCK_H
#define M_CORE_AUDIO_CLOCK_H

#include <mgba-util/common.h>

CXX_GUARD_START

/* This clock measures output samples in frontend (wall) time. It does not
 * control a mixer or alter the emulated machine's audio clock. */
struct mAudioClock {
	bool enabled;
	bool frontendRateKnown;
	bool unlimited;
	bool paused;
	double nominalRunRate;
	double frontendRunRate;
	double speedRatio;
	double outputSampleRate;
	uint64_t absoluteAudioSample;
	double fractionalAccumulator;
	uint64_t runStartSample;
	uint64_t runStartGameCycle;
	uint64_t runCycleSpan;
	uint32_t runAdvance;
	bool runActive;
};

/* Semantic input for the experimental MP2K bridge. The clock supplies sample
 * timestamps; the bridge owns event queueing and synthesis. */
enum mAudioClockEventType {
	M_AUDIO_CLOCK_PLAY_SONG,
	M_AUDIO_CLOCK_STOP_SONG,
	M_AUDIO_CLOCK_FADE_PLAYER
};
struct mAudioClockEvent {
	enum mAudioClockEventType type;
	int songId;
	int playerId;
	uint64_t gameCycle;
	uint64_t sequence;
	uint64_t sourceSequence;
	uint64_t audioSampleTimestamp;
	uint16_t fadeSpeed;
	uint64_t targetFirstTickAudioSample;
	bool targetFirstTickKnown;
	bool noSequencerTracks;
};

void mAudioClockInit(struct mAudioClock* clock, double nominalRunRate, double outputSampleRate);
void mAudioClockSetFrontendRate(struct mAudioClock* clock, bool known, double frontendRunRate);
uint32_t mAudioClockBeginRun(struct mAudioClock* clock, uint64_t gameCycle, uint64_t cycleSpan);
uint64_t mAudioClockTimestampForCycle(const struct mAudioClock* clock, uint64_t gameCycle);
void mAudioClockEndRun(struct mAudioClock* clock);

CXX_GUARD_END

#endif
