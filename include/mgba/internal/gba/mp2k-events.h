/* Copyright (c) 2026
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_MP2K_EVENTS_H
#define GBA_MP2K_EVENTS_H

#include <mgba-util/common.h>
#include <mgba/internal/gba/mp2k-player.h>

CXX_GUARD_START

struct ARMCore;
struct GBA;
struct mCore;
struct mAudioClock;
struct GBAMP2kProfile;
#define GBA_MP2K_MAX_PLAYERS 32

enum GBAMP2kFrontendThrottleMode {
	GBA_MP2K_THROTTLE_UNKNOWN,
	GBA_MP2K_THROTTLE_NORMAL,
	GBA_MP2K_THROTTLE_FRAME_STEP,
	GBA_MP2K_THROTTLE_FAST_FORWARD,
	GBA_MP2K_THROTTLE_SLOW_MOTION,
	GBA_MP2K_THROTTLE_REWIND,
	GBA_MP2K_THROTTLE_VSYNC,
	GBA_MP2K_THROTTLE_UNBLOCKED
};

struct GBAMP2kFrontendState {
	bool fastForwardKnown;
	bool fastForwardActive;
	bool fastForwardApiKnown;
	bool fastForwardApiActive;
	bool throttleStateKnown;
	enum GBAMP2kFrontendThrottleMode throttleMode;
	/* Target retro_run calls per second; 0 means unbounded, -1 unavailable. */
	float runRate;
};

enum GBAMP2kEventType {
	GBA_MP2K_SONG_START,
	GBA_MP2K_SONG_STOP,
	GBA_MP2K_MPLAY_START,
	GBA_MP2K_MPLAY_STOP,
	GBA_MP2K_MPLAY_CONTINUE,
	GBA_MP2K_FADE_OUT,
	GBA_MP2K_NATIVE_FIRST_TICK,
	GBA_MP2K_UNKNOWN_CALL
};

struct GBAMP2kEvent {
	enum GBAMP2kEventType type;
	int songId;
	int playerId;
	bool playerGuardPassed;
	uint16_t fadeSpeed;
	uint32_t functionAddress;
	uint32_t programCounter;
	uint64_t gbaCycle;
	bool fastForwardKnown;
	bool fastForwardState;
	float fastForwardRatio;
	bool frontendStateAvailable;
	struct GBAMP2kFrontendState frontend;
	uint64_t sequence;
	uint64_t parentSequence;
	uint64_t pcmSample;
	bool pcmSampleKnown;
	uint64_t audioSampleTimestamp;
	bool audioSampleTimestampKnown;
};

typedef void (*GBAMP2kEventSink)(const struct GBAMP2kEvent* event, void* context);

struct GBAMP2kEvents {
	struct GBAMP2kPlayerRegistry players;
	struct GBAMP2kEvent ramStarts[GBA_MP2K_MAX_PLAYERS];
	uint64_t ramFirstTickCycle[GBA_MP2K_MAX_PLAYERS];
	uint32_t ramStartReturnPC[GBA_MP2K_MAX_PLAYERS];
	bool suspended;
	/* Out-of-table Stop remains untrusted until a checked native no-op return. */
	uint32_t invalidStopPC, invalidStopReturn, invalidStopRamHash;
	uint64_t invalidStopSequence;
	struct GBAMP2kEvent invalidStopEvent;
	bool invalidStopDeferred, invalidStopRejectedHeader;
	uint32_t invalidPlayerPC, invalidPlayerReturn, invalidPlayerRamHash;
	uint32_t invalidPlayerComparePC, invalidPlayerExitPC;
	struct GBAMP2kEvent invalidPlayerEvent;
	bool invalidPlayerRejectedMagic, invalidPlayerDeferred;
	bool invalidPlayerHeaderFade;
	bool invalidPlayerAuxiliaryStop, invalidPlayerAcceptedMagic;
	uint32_t invalidPlayerAuxiliaryAddress, invalidPlayerAuxiliaryTracks;
	bool enabled;
	const struct GBAMP2kProfile* profile;
	bool runtimeTimingTrace;
	/* Optional read-only instruction probes; never semantic commands. */
	uint32_t lifetimeTraceStopPC, lifetimeTraceFinishPC;
	bool aorjLifetimeChecked, aorjNaturalFinish, aorjIndependentFinish;
	const struct GBA* gba;
	float baseFps;
	bool normalAudioWait;
	bool normalVideoWait;
	uint64_t sequence;
	uint64_t cycleEpoch;
	uint32_t lastCycle;
	bool cycleSeen;
	uint64_t parentSequence;
	uint64_t parentEventCycle;
	int parentSongId;
	uint32_t parentSongHeader;
	uint32_t parentFunctionAddress;
	int parentPlayerId;
	int activeSong[GBA_MP2K_MAX_PLAYERS];
	bool nativeTickTrace;
	bool firstTickWatch[GBA_MP2K_MAX_PLAYERS];
	bool firstTickArmed[GBA_MP2K_MAX_PLAYERS];
	uint32_t firstTickInitialTrack[GBA_MP2K_MAX_PLAYERS];
	int firstTickSong[GBA_MP2K_MAX_PLAYERS];
	uint64_t firstTickEventCycle[GBA_MP2K_MAX_PLAYERS];
	uint64_t firstTickSemanticSequence[GBA_MP2K_MAX_PLAYERS];
	bool nativeTickWatch;
	uint64_t nativeTickWatchUntil;
	uint32_t nativeTickLastTrack;
	uint32_t nativeTickLastClock;
	uint16_t nativeTickLastTempoC;
	unsigned nativeTickChanges;
	bool frontendStateAvailable;
	struct GBAMP2kFrontendState frontend;
	bool pcmTraceEnabled;
	uint64_t pcmSampleBase;
	struct mAudioClock* audioClock;
	GBAMP2kEventSink sink;
	void* sinkContext;
};

void GBAMP2kEventsReset(struct GBAMP2kEvents* events, const struct GBA* gba, float baseFps,
	bool normalAudioWait, bool normalVideoWait);
void GBAMP2kEventsObserve(struct GBAMP2kEvents* events, const struct ARMCore* cpu);
void GBAMP2kEventsUpdateFrontendState(struct GBAMP2kEvents* events, const struct GBAMP2kFrontendState* state);
void GBAMP2kEventsPosition(struct GBAMP2kEvents* events, uint64_t* cycle, uint64_t* pcmSample);
void GBAMP2kEventsSetPcmSampleBase(struct mCore* core, uint64_t submittedFrames);
void GBAMP2kEventsDisablePcmTrace(struct mCore* core);
bool GBAMP2kEventsTracePosition(struct mCore* core, uint64_t* cycle, uint64_t* pcmSample);
void GBAMP2kEventsSetAudioClock(struct mCore* core, struct mAudioClock* clock);
void GBAMP2kEventsSetSink(struct mCore* core, GBAMP2kEventSink sink, void* context);
bool GBAMP2kEventsEnabled(const struct mCore* core);
const struct GBAMP2kProfile* GBAMP2kEventsProfile(const struct mCore* core);
bool GBAMP2kEventsUseRuntimeProfile(struct mCore* core, const struct GBAMP2kProfile* profile);
/* Exact AORJ SE202 native FINE proof, consumed only at supported 2x/3x.
 * Identity and cancellation observation are prerequisites; inactive flags alone
 * cannot distinguish FINE from an explicit Stop after FINE. Rebind clears proof. */
bool GBAMP2kEventsTakeAorjNaturalFinish(struct mCore* core, unsigned player);
/* Enable the exact known route without a process environment opt-in. */
bool GBAMP2kEventsEnableKnownProfile(struct mCore* core);
/* Discard observation history after a native state load; seed only loaded RAM. */
bool GBAMP2kEventsRebind(struct mCore* core, const int* activeSongs);
/* Keep profile identity while bypassing instruction watches until a fresh rebind. */
void GBAMP2kEventsSuspend(struct mCore* core);
void GBAMP2kEventsSetFrontendState(struct mCore* core, const struct GBAMP2kFrontendState* state);

CXX_GUARD_END

#endif
