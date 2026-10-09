/* Copyright (c) 2026
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/mp2k-events.h>
#include <mgba/internal/gba/mp2k-profile.h>

#include <mgba/core/log.h>
#include <mgba/core/sync.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/core/audio-clock.h>
#include <mgba-util/audio-buffer.h>
#include <string.h>
#include <stdlib.h>

mLOG_DEFINE_CATEGORY(GBA_MP2K_EVENTS, "GBA MP2K Events", "gba.mp2k.events");

static uint16_t _read16(const uint8_t* data, size_t offset) {
	return data[offset] | data[offset + 1] << 8;
}

static uint32_t _read32(const uint8_t* data, size_t offset) {
	return (uint32_t) _read16(data, offset) | (uint32_t) _read16(data, offset + 2) << 16;
}

static bool _romPointer(const struct GBA* gba, uint32_t address, size_t length) {
	if (address < 0x08000000 || address >= 0x08000000 + gba->memory.romSize) {
		return false;
	}
	return length <= gba->memory.romSize - (address - 0x08000000);
}

static int _playerFromPointer(const struct GBAMP2kEvents* events, uint32_t pointer) {
	if (!pointer) return -1;
	const uint8_t* rom = (const uint8_t*) events->gba->memory.rom;
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		if (_read32(rom, events->profile->playerTableOffset + i * 12) == pointer) {
			return (int) i;
		}
	}
	return -1;
}

static int _songFromHeader(const struct GBAMP2kEvents* events, uint32_t header) {
	if (!_romPointer(events->gba, header, 8)) return -1;
	const uint8_t* rom = (const uint8_t*) events->gba->memory.rom;
	for (unsigned i = 0; i < events->profile->songCount; ++i) {
		if (_read32(rom, events->profile->songTableOffset + i * 8) == header) {
			return (int) i;
		}
	}
	return -1;
}

/* Read only explicit validated allocations, including idle slots. Never use
 * an invalid song-table-derived player pointer for host memory access. */
static uint32_t _eventRamDiagnostic(const struct GBAMP2kEvents* events, const char* phase) {
	const uint8_t* rom = (const void*) events->gba->memory.rom;
	uint32_t hash = UINT32_C(2166136261);
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		size_t entry = events->profile->playerTableOffset + i * 12;
		uint32_t address = _read32(rom, entry), tracks = _read32(rom, entry + 4);
		unsigned count = _read16(rom, entry + 8);
		const struct GBAMP2kMusicPlayerInfo* player = GBAMP2kPlayerAt(events->gba, events->profile, i);
		const uint8_t* data = GBAMP2kPlayerRam(events->gba, tracks, count * sizeof(struct GBAMP2kMusicPlayerTrack));
		if (phase && player) mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K INVALID EVENT RAM] phase=%s game=%s generation=%u player=%u address=%08x header=%08x staticSong=%d runtimeSong=%d initialized=%d status=%08x playing=%d paused=%d stopped=%d magic=%08x tracks=%08x/%08x count=%u/%u clock=%u tempo=%u/%u",
			phase, events->profile->gameCode, events->players.generation, i, address, player->songHeader,
			_songFromHeader(events, player->songHeader), events->activeSong[i], player->magic == MP2K_MAGIC,
			player->status, player->songHeader && !(player->status & 0x80000000U),
			!!(player->status & 0x80000000U), !player->songHeader, player->magic,
			player->tracks, tracks, player->trackCount, count, player->clock, player->tempoI, player->tempoC);
		if (phase && data) for (unsigned j = 0; j < count; ++j) {
			const struct GBAMP2kMusicPlayerTrack* t = (const void*) (data + j * sizeof(*t));
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K INVALID EVENT TRACK] phase=%s player=%u track=%u address=%08x flags=%02x cursor=%08x wait=%u running=%02x stackDepth=%u",
				phase, i, j, tracks + j * (unsigned) sizeof(*t), t->flags, t->cmdPtr, t->wait, t->runningStatus, t->patternLevel);
		}
		const uint8_t* prefix = (const void*) player;
		for (size_t j = 0; prefix && j < sizeof(*player); ++j) hash = (hash ^ prefix[j]) * UINT32_C(16777619);
		for (size_t j = 0; data && j < count * sizeof(struct GBAMP2kMusicPlayerTrack); ++j) hash = (hash ^ data[j]) * UINT32_C(16777619);
	}
	return hash;
}

/* Exact native Stop wrapper: read selected/current headers, compare, and skip
 * MPlayStop on mismatch. Literal tables and the BL target must also match the
 * validated profile. This does not grant validity to an out-of-table song. */
static bool _checkedStopWrapper(const struct GBAMP2kEvents* events, uint32_t address) {
	static const uint16_t prefix[] = {
		0xB500, 0x0400, 0x4A09, 0x490A, 0x0B40, 0x1840, 0x8883,
		0x0059, 0x18C9, 0x0089, 0x1889, 0x680A, 0x6811, 0x6800,
		0x4281, 0xD102, 0x1C10
	};
	if (!_romPointer(events->gba, address, 52)) return false;
	const uint8_t* bytes = (const uint8_t*) events->gba->memory.rom + address - 0x08000000;
	for (unsigned i = 0; i < sizeof(prefix) / sizeof(*prefix); ++i)
		if (_read16(bytes, i * 2) != prefix[i]) return false;
	uint16_t high = _read16(bytes, 34), low = _read16(bytes, 36);
	if ((high & 0xF800) != 0xF000 || (low & 0xF800) != 0xF800 ||
	    _read16(bytes, 38) != 0xBC01 || _read16(bytes, 40) != 0x4700 ||
	    _read32(bytes, 44) != 0x08000000 + events->profile->playerTableOffset ||
	    _read32(bytes, 48) != 0x08000000 + events->profile->songTableOffset) return false;
	int32_t displacement = (int32_t) (high & 0x7FF) * 4096;
	if (displacement & 0x400000) displacement -= 0x800000;
	uint32_t target = address + 38 + displacement + (low & 0x7FF) * 2;
	const struct GBAMP2kFunction* stop = GBAMP2kProfileFunctionAt(events->profile, target);
	return stop && stop->type == GBA_MP2K_MPLAY_STOP && stop->argument == GBA_MP2K_ARG_PLAYER;
}

/* A checked native Fade magic guard is needed for both a reserved NULL slot
 * and a known ROM song header. Neither argument is a player mapping. */
static bool _checkedFadeMagicGuard(const struct GBAMP2kEvents* events, uint32_t address) {
	static const uint16_t code[] = { 0x1C02, 0x0409, 0x0C09, 0x6B53, 0x4804, 0x4283,
		0xD104, 0x84D1, 0x8491, 0x2080, 0x0040, 0x8510, 0x4770 };
	if (events->profile->playerBacking != GBA_MP2K_RAM_PLAYER || !_romPointer(events->gba, address, 32)) return false;
	const uint8_t* rom = (const void*) events->gba->memory.rom;
	const uint8_t* bytes = rom + address - 0x08000000;
	for (unsigned i = 0; i < sizeof(code) / sizeof(*code); ++i)
		if (_read16(bytes, i * 2) != code[i]) return false;
	if (_read32(bytes, 28) != MP2K_MAGIC) return false;
	return true;
}

static bool _checkedReservedNullFade(const struct GBAMP2kEvents* events, uint32_t address) {
	if (!_checkedFadeMagicGuard(events, address)) return false;
	const uint8_t* rom = (const void*) events->gba->memory.rom;
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		size_t at = events->profile->playerTableOffset + i * 12;
		if (!_read32(rom, at) && !_read32(rom, at + 4) && !_read32(rom, at + 8)) return true;
	}
	return false;
}

/* Some callers pass a ROM song header (possibly from an earlier song) to Fade.
 * This is never a player mapping: require a known validated header, a confirmed
 * validated runtime witness and the exact native
 * no-write magic guard, then still witness its rejection and unchanged return. */
static int _knownHeaderFadeWitness(struct GBAMP2kEvents* events, uint32_t address, uint32_t header) {
	if (events->players.invalidated || !_checkedFadeMagicGuard(events, address) || !_romPointer(events->gba, header, 0x38) ||
	    _songFromHeader(events, header) < 0 ||
	    !GBAMP2kProfileValidate(events->profile, events->gba) ||
	    _read32((const uint8_t*) events->gba->memory.rom, header - 0x08000000 + 0x34) == MP2K_MAGIC) return -1;
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		const struct GBAMP2kRuntimePlayer* slot = &events->players.slots[i];
		if (!slot->startConfirmed || slot->reserved || slot->generation != events->players.generation) continue;
		const struct GBAMP2kMusicPlayerInfo* player = GBAMP2kPlayerAt(events->gba, events->profile, i);
		if (player && _songFromHeader(events, player->songHeader) >= 0 &&
		    GBAMP2kPlayerRefresh(&events->players, events->gba, events->profile, i) == 1) return (int) i;
	}
	return -1;
}

static bool _checkedReservedNullStop(const struct GBAMP2kEvents* events, uint32_t address) {
	static const uint16_t prefix[] = { 0xB570, 0x1C06, 0x6B71, 0x480D, 0x4281, 0xD114 };
	if (events->profile->playerBacking != GBA_MP2K_RAM_PLAYER || !_romPointer(events->gba, address, 64)) return false;
	const uint8_t* rom = (const void*) events->gba->memory.rom;
	const uint8_t* bytes = rom + address - 0x08000000;
	for (unsigned i = 0; i < sizeof(prefix) / sizeof(*prefix); ++i)
		if (_read16(bytes, i * 2) != prefix[i]) return false;
	/* Magic mismatch branches directly to POP r4/r5/r6; POP r0; BX r0.
	 * Every write and channel-stop call lies before this checked epilogue. */
	if (_read16(bytes, 54) != 0xBC70 || _read16(bytes, 56) != 0xBC01 ||
	    _read16(bytes, 58) != 0x4700 || _read32(bytes, 60) != MP2K_MAGIC) return false;
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		size_t at = events->profile->playerTableOffset + i * 12;
		if (!_read32(rom, at) && !_read32(rom, at + 4) && !_read32(rom, at + 8)) return true;
	}
	return false;
}

/* All-Stop's saved r5 is the actual ROM table entry, not a guessed RAM stride. */
static int _reservedNullTableSlot(const struct GBAMP2kEvents* events, uint32_t entry) {
	uint32_t base = 0x08000000 + events->profile->playerTableOffset;
	if (entry < base || (entry - base) % 12 || (entry - base) / 12 >= events->profile->playerCount ||
	    !_romPointer(events->gba, entry, 12)) return -1;
	unsigned slot = (entry - base) / 12;
	const uint8_t* bytes = (const uint8_t*) events->gba->memory.rom + events->profile->playerTableOffset + slot * 12;
	return !_read32(bytes, 0) && !_read32(bytes, 4) && !_read32(bytes, 8) ? (int) slot : -1;
}

#include "mp2k-auxiliary.inc"

static const char* _invalidPlayerScope(const struct GBAMP2kEvents* events) {
	return events->invalidPlayerAuxiliaryStop ? "AUXILIARY IDLE" :
		events->invalidPlayerHeaderFade ? "ROM HEADER" : "RESERVED NULL";
}

/* RAM starts use the checked explicit table address in the native r0.
 * Confirmed live mappings describe later commands; static defaults are only
 * metadata and must never override an accepted native start. */
static void _ramPlayerResolved(const struct GBAMP2kEvents* events, const struct GBAMP2kEvent* start) {
	if (!events->players.diagnostics) return;
	const uint8_t* rom = (const uint8_t*) events->gba->memory.rom;
	unsigned defaultPlayer = _read16(rom, events->profile->songTableOffset + start->songId * 8 + 4);
	mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_RESOLVE] game=%s generation=%u song=%d staticPlayer=%u runtimeR0=%08x runtimeR1=%08x resolvedPlayer=%d reason=MPLAYSTART_R0",
		events->profile->gameCode, events->players.generation, start->songId,
		defaultPlayer, events->players.slots[start->playerId].address,
		events->players.slots[start->playerId].lastHeader, start->playerId);
}

static const char* _typeName(enum GBAMP2kEventType type) {
	switch (type) {
	case GBA_MP2K_SONG_START: return "SONG_START";
	case GBA_MP2K_SONG_STOP: return "SONG_STOP";
	case GBA_MP2K_MPLAY_START: return "MPLAY_START";
	case GBA_MP2K_MPLAY_STOP: return "MPLAY_STOP";
	case GBA_MP2K_MPLAY_CONTINUE: return "MPLAY_CONTINUE";
	case GBA_MP2K_FADE_OUT: return "FADE_OUT";
	default: return "UNKNOWN_MP2K_CALL";
	}
}

static const char* _throttleName(enum GBAMP2kFrontendThrottleMode mode) {
	switch (mode) {
	case GBA_MP2K_THROTTLE_NORMAL: return "NORMAL";
	case GBA_MP2K_THROTTLE_FRAME_STEP: return "FRAME_STEP";
	case GBA_MP2K_THROTTLE_FAST_FORWARD: return "FF";
	case GBA_MP2K_THROTTLE_SLOW_MOTION: return "SLOW";
	case GBA_MP2K_THROTTLE_REWIND: return "REWIND";
	case GBA_MP2K_THROTTLE_VSYNC: return "VSYNC";
	case GBA_MP2K_THROTTLE_UNBLOCKED: return "UNBLOCKED";
	default: return "UNKNOWN";
	}
}

void GBAMP2kEventsUpdateFrontendState(struct GBAMP2kEvents* events, const struct GBAMP2kFrontendState* state) {
	if (!events->enabled || !state) {
		return;
	}
	const struct GBAMP2kFrontendState* old = &events->frontend;
	float rateChange = old->runRate - state->runRate;
	if (rateChange < 0.f) {
		rateChange = -rateChange;
	}
	bool changed = !events->frontendStateAvailable ||
		old->fastForwardKnown != state->fastForwardKnown ||
		old->fastForwardActive != state->fastForwardActive ||
		old->fastForwardApiKnown != state->fastForwardApiKnown ||
		old->fastForwardApiActive != state->fastForwardApiActive ||
		old->throttleStateKnown != state->throttleStateKnown ||
		old->throttleMode != state->throttleMode || rateChange > 0.01f;
	events->frontend = *state;
	events->frontendStateAvailable = true;
	if (changed) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K FRONTEND] ff=%d mode=%s rate=%.3f throttle=%d legacyFF=%d",
			state->fastForwardKnown ? (int) state->fastForwardActive : -1,
			_throttleName(state->throttleMode), state->runRate, (int) state->throttleStateKnown,
			state->fastForwardApiKnown ? (int) state->fastForwardApiActive : -1);
	}
}

static bool _parentCall(const struct GBAMP2kEvents* events, uint32_t returnAddress) {
	const struct GBAMP2kFunction* parent = GBAMP2kProfileFunctionAt(events->profile,
		events->parentFunctionAddress);
	return parent && parent->parentReturnEnd &&
		returnAddress >= parent->address && returnAddress < parent->parentReturnEnd;
}

void GBAMP2kEventsReset(struct GBAMP2kEvents* events, const struct GBA* gba, float baseFps,
	bool normalAudioWait, bool normalVideoWait) {
	uint32_t generation = events->players.generation;
	memset(events, 0, sizeof(*events));
	events->players.generation = generation;
	events->gba = gba;
	events->baseFps = baseFps > 0 ? baseFps : 60.f;
	events->normalAudioWait = normalAudioWait;
	events->normalVideoWait = normalVideoWait;
	const char* trace = getenv("MGBA_MP2K_EVENT_TRACE");
	const char* audioTrace = getenv("MGBA_MP2K_AUDIO_TRACE");
	const char* candidate = getenv("MGBA_FIXED_AUDIO_PROTOTYPE");
	const char* output = getenv("MGBA_FIXED_AUDIO_TEMPO");
	if ((!trace || strcmp(trace, "1")) && (!audioTrace || strcmp(audioTrace, "1")) &&
	    (!candidate || strcmp(candidate, "1")) && (!output || strcmp(output, "1"))) {
		return;
	}
	events->profile = GBAMP2kKnownProfile(gba);
	if (!events->profile) return;
	struct mLogger* logger = mLogGetContext();
	if (logger && logger->filter) {
		mLogFilterSet(logger->filter, "gba.mp2k.events", mLOG_INFO | mLOG_WARN | mLOG_ERROR | mLOG_FATAL);
	}
	mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K] detected game=%s", events->profile->gameCode);
	if (!GBAMP2kProfileValidate(events->profile, gba)) {
		mLOG(GBA_MP2K_EVENTS, WARN, "[MP2K] %s song table validation failed", events->profile->name);
		return;
	}
	events->enabled = true;
	const char* nativeTickTrace = getenv("MGBA_MP2K_NATIVE_TICK_TRACE");
	events->nativeTickTrace = nativeTickTrace && !strcmp(nativeTickTrace, "1");
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		events->activeSong[i] = -1;
	}
	mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K] %s song table validated offset=0x%X entries=%u players=%u",
		events->profile->gameCode, events->profile->songTableOffset,
		events->profile->songCount, events->profile->playerCount);
}

static uint64_t _currentCycle(struct GBAMP2kEvents* events) {
#ifndef ENABLE_DEBUGGERS
	/* Minimal libretro builds do not advance mTiming.globalCycles. */
	uint32_t currentCycle = (uint32_t) mTimingCurrentTime(&events->gba->timing);
	if (events->cycleSeen && currentCycle < events->lastCycle &&
	    events->lastCycle - currentCycle > 0x80000000U) {
		events->cycleEpoch += 0x100000000ULL;
	}
	events->lastCycle = currentCycle;
	events->cycleSeen = true;
	return events->cycleEpoch + currentCycle;
#else
	return mTimingGlobalTime(&events->gba->timing);
#endif
}

void GBAMP2kEventsPosition(struct GBAMP2kEvents* events, uint64_t* cycle, uint64_t* pcmSample) {
	if (cycle) {
		*cycle = _currentCycle(events);
	}
	if (pcmSample) {
		*pcmSample = events->pcmSampleBase + mAudioBufferAvailable(&events->gba->audio.psg.buffer);
	}
}

void GBAMP2kEventsObserve(struct GBAMP2kEvents* events, const struct ARMCore* cpu) {
	uint64_t currentCycle = _currentCycle(events);
	uint32_t observedPC = _ARMPCAddress((struct ARMCore*) cpu);
	if (events->invalidPlayerDeferred && currentCycle - events->invalidPlayerEvent.gbaCycle > 280896) {
		events->invalidPlayerDeferred = false;
		mLOG(GBA_MP2K_EVENTS, WARN, "[MP2K %s %s REJECT] detailed_reason=NATIVE_RETURN_TIMEOUT sequence=%llu",
			_invalidPlayerScope(events),
			events->invalidPlayerEvent.type == GBA_MP2K_MPLAY_STOP ? "STOP" : "FADE", (unsigned long long) events->invalidPlayerEvent.sequence);
		if (events->sink) events->sink(&events->invalidPlayerEvent, events->sinkContext);
	}
	if (events->invalidPlayerPC && (observedPC == events->invalidPlayerComparePC ||
	    observedPC == events->invalidPlayerExitPC || observedPC == events->invalidPlayerReturn)) {
		const char* command = events->invalidPlayerEvent.type == GBA_MP2K_MPLAY_STOP ? "STOP" : "FADE";
		const char* scope = _invalidPlayerScope(events);
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K %s %s TRACE] sequence=%llu pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x generation=%u",
			scope, command, (unsigned long long) events->invalidPlayerEvent.sequence, observedPC, cpu->gprs[0], cpu->gprs[1], cpu->gprs[2], cpu->gprs[3], cpu->gprs[4], cpu->gprs[5], cpu->gprs[6], events->players.generation);
		if (observedPC == events->invalidPlayerComparePC) {
			events->invalidPlayerRejectedMagic = cpu->gprs[0] == MP2K_MAGIC &&
				(events->invalidPlayerEvent.type == GBA_MP2K_MPLAY_STOP ? cpu->gprs[1] : cpu->gprs[3]) != MP2K_MAGIC;
			events->invalidPlayerAcceptedMagic = events->invalidPlayerAuxiliaryStop &&
				cpu->gprs[0] == MP2K_MAGIC && cpu->gprs[1] == MP2K_MAGIC;
		}
		if (observedPC == events->invalidPlayerReturn) {
			uint32_t after = _invalidPlayerRamHash(events, events->players.diagnostics ?
				(events->invalidPlayerHeaderFade ? "AFTER_ROM_HEADER_FADE" : "AFTER_NULL_FADE") : NULL);
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K %s %s RETURN] sequence=%llu rejectedMagic=%d unchanged=%d ramBefore=%08x ramAfter=%08x",
				scope, command, (unsigned long long) events->invalidPlayerEvent.sequence, events->invalidPlayerRejectedMagic,
				after == events->invalidPlayerRamHash, events->invalidPlayerRamHash, after);
			if (events->invalidPlayerAuxiliaryStop)
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K AUXILIARY IDLE STOP PROOF] sequence=%llu acceptedMagic=%d unchanged=%d address=%08x tracks=%08x ramBefore=%08x ramAfter=%08x",
					(unsigned long long) events->invalidPlayerEvent.sequence, events->invalidPlayerAcceptedMagic,
					after == events->invalidPlayerRamHash, events->invalidPlayerAuxiliaryAddress,
					events->invalidPlayerAuxiliaryTracks, events->invalidPlayerRamHash, after);
			if (events->invalidPlayerDeferred) {
				events->invalidPlayerDeferred = false;
				bool noop = (events->invalidPlayerAuxiliaryStop ? events->invalidPlayerAcceptedMagic : events->invalidPlayerRejectedMagic) && after == events->invalidPlayerRamHash;
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K %s %s %s] sequence=%llu detailed_reason=%s game=%s generation=%u",
					scope, command, noop ? "NOOP" : "REJECT", (unsigned long long) events->invalidPlayerEvent.sequence,
					noop ? (events->invalidPlayerAuxiliaryStop ? "NATIVE_IDLE_STOP_UNCHANGED_RAM" : "NATIVE_MAGIC_REJECT_UNCHANGED_RAM") : "UNPROVEN_NATIVE_NOOP", events->profile->gameCode, events->players.generation);
				if (!noop && events->sink) events->sink(&events->invalidPlayerEvent, events->sinkContext);
			}
			events->invalidPlayerPC = 0;
		}
	}
	if (events->invalidStopDeferred && currentCycle - events->invalidStopEvent.gbaCycle > 280896) {
		events->invalidStopDeferred = false;
		mLOG(GBA_MP2K_EVENTS, WARN, "[MP2K INVALID STOP REJECT] detailed_reason=NATIVE_RETURN_TIMEOUT sequence=%llu",
			(unsigned long long) events->invalidStopSequence);
		if (events->sink) events->sink(&events->invalidStopEvent, events->sinkContext);
	}
	if (events->invalidStopPC && (observedPC == events->invalidStopPC + 0x1C ||
	    observedPC == events->invalidStopPC + 0x26 || observedPC == events->invalidStopReturn)) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K INVALID STOP TRACE] sequence=%llu pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x generation=%u",
			(unsigned long long) events->invalidStopSequence, observedPC, cpu->gprs[0], cpu->gprs[1], cpu->gprs[2], cpu->gprs[3], events->players.generation);
		if (observedPC == events->invalidStopPC + 0x1C)
			events->invalidStopRejectedHeader = cpu->gprs[0] != cpu->gprs[1];
		if (observedPC == events->invalidStopReturn) {
			uint32_t after = _eventRamDiagnostic(events, events->players.diagnostics ? "AFTER" : NULL);
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K INVALID STOP RETURN] sequence=%llu ramBefore=%08x ramAfter=%08x unchanged=%d",
				(unsigned long long) events->invalidStopSequence, events->invalidStopRamHash, after, after == events->invalidStopRamHash);
			if (events->invalidStopDeferred) {
				events->invalidStopDeferred = false;
				bool noop = events->invalidStopRejectedHeader && after == events->invalidStopRamHash;
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K INVALID STOP %s] sequence=%llu song=%d detailed_reason=%s generation=%u",
					noop ? "NOOP" : "REJECT", (unsigned long long) events->invalidStopSequence,
					events->invalidStopEvent.songId, noop ? "NATIVE_HEADER_MISMATCH_UNCHANGED_RAM" : "UNPROVEN_NATIVE_NOOP", events->players.generation);
				if (!noop && events->sink) events->sink(&events->invalidStopEvent, events->sinkContext);
			}
			events->invalidStopPC = 0;
		}
	}
	/* Runtime profiles observe the first track change to schedule candidate ticks.
	 * This timing evidence does not establish audio ownership. */
	bool timingProbe = events->runtimeTimingTrace && !events->profile->known &&
		(_romPointer(events->gba, observedPC, 2) ||
		 (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER &&
		  observedPC >= 0x03000000 && observedPC <= 0x03008000 - 4));
	if (events->sink && (timingProbe ||
	    (observedPC >= events->profile->driverCodeStart &&
	     observedPC < events->profile->firstTickCodeEnd))) {
		const uint8_t* rom = (const uint8_t*) events->gba->memory.rom;
		for (unsigned playerId = 0; playerId < events->profile->playerCount; ++playerId) {
			if (!events->firstTickWatch[playerId]) continue;
			if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER &&
			    currentCycle - events->firstTickEventCycle[playerId] > UINT64_C(280896) * 8) {
				/* Rejected/disabled starts never arm output. Bound observation
				 * without changing the native game or treating it as corruption. */
				events->firstTickWatch[playerId] = false;
				if (events->players.diagnostics) mLOG(GBA_MP2K_EVENTS, INFO,
					"[RAM_PLAYER_START_UNCONFIRMED] generation=%u player=%u song=%d", events->players.generation, playerId, events->firstTickSong[playerId]);
				continue;
			}

			const struct GBAMP2kMusicPlayerInfo* player = GBAMP2kPlayerAt(events->gba, events->profile, playerId);
			if (!player) continue;
			if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER) {
				struct GBAMP2kEvent* start = &events->ramStarts[playerId];
				uint32_t header = _read32(rom, events->profile->songTableOffset + start->songId * 8);
				/* A zero-track start has no sequencer tick. Confirm at the
				 * native return boundary and use the existing silent-start API. */
				if (!rom[header - 0x08000000] && observedPC == events->ramStartReturnPC[playerId] &&
				    GBAMP2kPlayerConfirm(&events->players, events->gba, events->profile, playerId, start->songId, header)) {
					events->firstTickWatch[playerId] = false;
					_ramPlayerResolved(events, start);
					events->activeSong[playerId] = start->songId;
					events->sink(start, events->sinkContext);
					continue;
				}
			}
			uint32_t trackAddress = player->tracks;
			const struct GBAMP2kMusicPlayerTrack* track;
			if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER &&
			    !GBAMP2kPlayerRam(events->gba, trackAddress, sizeof(*track))) continue;
			if (trackAddress >= 0x02000000 &&
			    trackAddress <= 0x02040000 - sizeof(*track)) {
				track = (const void*) ((const uint8_t*) events->gba->memory.wram + trackAddress - 0x02000000);
			} else if (trackAddress >= 0x03000000 &&
			           trackAddress <= 0x03008000 - sizeof(*track)) {
				track = (const void*) ((const uint8_t*) events->gba->memory.iwram + trackAddress - 0x03000000);
			} else continue;
			if (track->cmdPtr == events->firstTickInitialTrack[playerId]) {
				if (events->players.diagnostics && !events->firstTickArmed[playerId])
					mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_FIRST_TICK_ARM] player=%u pc=%08x track=%08x expected=%08x header=%08x flags=%02x",playerId,observedPC,track->cmdPtr,events->firstTickInitialTrack[playerId],player->songHeader,track->flags);
				events->firstTickArmed[playerId] = true;
				continue;
			}
			if (!events->firstTickArmed[playerId]) continue;
			if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER) {
				struct GBAMP2kEvent* start = &events->ramStarts[playerId];
				uint32_t header = _read32(rom, events->profile->songTableOffset + start->songId * 8);
				if (!events->ramFirstTickCycle[playerId]) events->ramFirstTickCycle[playerId] = currentCycle;
				if (player->magic != MP2K_MAGIC) continue;
				if (events->players.diagnostics) mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_FIRST_TICK_CONFIRM] player=%u track=%08x header=%08x/%08x song=%d tempo=%u status=%08x",playerId,track->cmdPtr,player->songHeader,header,start->songId,player->tempoI,player->status);
				if (!GBAMP2kPlayerConfirm(&events->players, events->gba, events->profile, playerId, start->songId, header)) continue;
				_ramPlayerResolved(events, start);
				/* Hook entry can be rejected by priority. Only an accepted native
				 * header and first track advance may reach the external player. */
				events->activeSong[playerId] = start->songId;
				events->sink(start, events->sinkContext);
			}
			events->firstTickWatch[playerId] = false;
			struct GBAMP2kEvent tick = { 0 };
			tick.type = GBA_MP2K_NATIVE_FIRST_TICK;
			tick.playerId = playerId;
			tick.songId = events->firstTickSong[playerId];
			tick.parentSequence = events->firstTickSemanticSequence[playerId];
			tick.gbaCycle = events->profile->playerBacking == GBA_MP2K_RAM_PLAYER ? events->ramFirstTickCycle[playerId] : currentCycle;
			tick.audioSampleTimestampKnown = events->audioClock && events->audioClock->enabled;
			if (tick.audioSampleTimestampKnown) {
				tick.audioSampleTimestamp = mAudioClockTimestampForCycle(events->audioClock, tick.gbaCycle);
			}
			mLOG(GBA_MP2K_EVENTS, INFO,
				"[MP2K FIRST TICK] song=%d player=%u eventCycle=%llu nextNativeTickCycle=%llu cyclesUntilTick=%llu audioSample=%llu",
				tick.songId, playerId, (unsigned long long) events->firstTickEventCycle[playerId],
				(unsigned long long) currentCycle,
				(unsigned long long) (currentCycle - events->firstTickEventCycle[playerId]),
				(unsigned long long) tick.audioSampleTimestamp);
			events->sink(&tick, events->sinkContext);
			if (timingProbe) {
				mLOG(GBA_MP2K_EVENTS, INFO,
					"[MP2K TIMING PROBE] song=%d player=%u pc=%08x track=%08x confidence=observed-track-change",
					tick.songId, playerId, observedPC, track->cmdPtr);
			}
		}
	}
	if (events->nativeTickWatch && events->profile->playerCount > 4) {
		if (currentCycle > events->nativeTickWatchUntil || events->nativeTickChanges >= 24) {
			events->nativeTickWatch = false;
		} else {
			const struct GBA* gba = events->gba;
			uint32_t address = _read32((const uint8_t*) gba->memory.rom,
				events->profile->playerTableOffset + 4 * 12);
			if (address >= 0x03000000 && address <= 0x03008000 - sizeof(struct GBAMP2kMusicPlayerInfo)) {
				const struct GBAMP2kMusicPlayerInfo* player = (const void*)
					((const uint8_t*) gba->memory.iwram + address - 0x03000000);
				uint32_t position = 0;
				if (player->tracks >= 0x02000000 &&
				    player->tracks <= 0x02040000 - sizeof(struct GBAMP2kMusicPlayerTrack)) {
					const struct GBAMP2kMusicPlayerTrack* track = (const void*)
						((const uint8_t*) gba->memory.wram + player->tracks - 0x02000000);
					position = track->cmdPtr;
				}
				if (position != events->nativeTickLastTrack ||
				    player->clock != events->nativeTickLastClock ||
				    player->tempoC != events->nativeTickLastTempoC) {
				uint32_t pc = _ARMPCAddress((struct ARMCore*) cpu);
				mLOG(GBA_MP2K_EVENTS, INFO,
					"[MP2K NATIVE TICK] cycle=%llu audioSample=%llu pc=%08x clock=%u tempo=%u/%u/%u/%u track0=%08x status=%08x",
					(unsigned long long) currentCycle,
					(unsigned long long) (events->audioClock ? mAudioClockTimestampForCycle(events->audioClock, currentCycle) : 0),
					pc, player->clock, player->tempoD, player->tempoU,
					player->tempoI, player->tempoC, position, player->status);
				events->nativeTickLastTrack = position;
				events->nativeTickLastClock = player->clock;
				events->nativeTickLastTempoC = player->tempoC;
				++events->nativeTickChanges;
			}
			}
		}
	}
	if (cpu->executionMode != MODE_THUMB) {
		return;
	}
	uint32_t pc = _ARMPCAddress((struct ARMCore*) cpu);
	const struct GBAMP2kFunction* function = GBAMP2kProfileFunctionAt(events->profile, pc);
	if (!function) return;
	if (events->invalidPlayerDeferred) {
		events->invalidPlayerDeferred = false;
		mLOG(GBA_MP2K_EVENTS, WARN, "[MP2K %s %s REJECT] detailed_reason=NESTED_NATIVE_COMMAND pc=%08x sequence=%llu",
			_invalidPlayerScope(events),
			events->invalidPlayerEvent.type == GBA_MP2K_MPLAY_STOP ? "STOP" : "FADE", pc, (unsigned long long) events->invalidPlayerEvent.sequence);
		if (events->sink) events->sink(&events->invalidPlayerEvent, events->sinkContext);
	}
	if (events->invalidStopDeferred) {
		/* A nested audio command means the outer invalid song was not a no-op.
		 * Retain the original HARD guard before processing any such command. */
		events->invalidStopDeferred = false;
		mLOG(GBA_MP2K_EVENTS, WARN, "[MP2K INVALID STOP REJECT] detailed_reason=NESTED_NATIVE_COMMAND pc=%08x sequence=%llu", pc,
			(unsigned long long) events->invalidStopSequence);
		if (events->sink) events->sink(&events->invalidStopEvent, events->sinkContext);
	}
	const struct GBA* gba = events->gba;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	uint32_t arg0 = cpu->gprs[0];
	uint32_t arg1 = cpu->gprs[1];
	uint32_t returnAddress = cpu->gprs[ARM_LR] & ~1U;
	struct GBAMP2kEvent event = { 0 };
	event.type = function->type;
	event.functionAddress = function->address;
	event.programCounter = pc;
	event.gbaCycle = currentCycle;
	event.sequence = ++events->sequence;
	event.playerId = -1;
	event.songId = -1;
	event.fastForwardRatio = -1.f;
	event.frontendStateAvailable = events->frontendStateAvailable;
	event.frontend = events->frontend;
	event.pcmSampleKnown = events->pcmTraceEnabled;
	event.audioSampleTimestampKnown = events->audioClock && events->audioClock->enabled;
	if (event.audioSampleTimestampKnown) {
		event.audioSampleTimestamp = mAudioClockTimestampForCycle(events->audioClock, event.gbaCycle);
	}
	if (event.pcmSampleKnown) {
		GBAMP2kEventsPosition(events, NULL, &event.pcmSample);
	}
	if (gba->sync) {
		event.fastForwardKnown = true;
		event.fastForwardRatio = gba->sync->fpsTarget / events->baseFps;
		event.fastForwardState = event.fastForwardRatio > 1.01f ||
			(events->normalAudioWait && !gba->sync->audioWait) ||
			(events->normalVideoWait && !gba->sync->videoFrameWait);
		if (!event.fastForwardState) {
			event.fastForwardRatio = 1.f;
		} else if (event.fastForwardRatio <= 1.01f) {
			event.fastForwardRatio = -1.f;
		}
	}
	if (event.frontendStateAvailable) {
		event.fastForwardKnown = event.frontend.fastForwardKnown;
		event.fastForwardState = event.frontend.fastForwardActive;
		event.fastForwardRatio = event.frontend.runRate > 0.f ?
			event.frontend.runRate / events->baseFps : -1.f;
	}
	if (function->argument == GBA_MP2K_ARG_SONG) {
		event.songId = (int) (arg0 & 0xFFFF);
		if (event.songId >= 0 && (unsigned) event.songId < events->profile->songCount) {
			size_t offset = events->profile->songTableOffset + event.songId * 8;
			event.playerId = _read16(rom, offset + 4);
			events->parentSongHeader = _read32(rom, offset);
		} else {
			events->parentSongHeader = 0;
			event.type = GBA_MP2K_UNKNOWN_CALL;
			mLOG(GBA_MP2K_EVENTS, WARN, "[MP2K INVALID EVENT] game=%s profile=%s sequence=%llu function=%s type=%u detailed_reason=SONG_INDEX_OOB song=%d songCount=%u player=%d r0=%08x r1=%08x pc=%08x return=%08x generation=%u",
				events->profile->gameCode, events->profile->name, (unsigned long long) event.sequence,
				function->name, function->type, event.songId, events->profile->songCount, event.playerId,
				arg0, arg1, pc, returnAddress, events->players.generation);
			if (function->type == GBA_MP2K_SONG_STOP && events->profile->playerBacking == GBA_MP2K_RAM_PLAYER &&
			    _checkedStopWrapper(events, pc)) {
				events->invalidStopPC = pc;
				events->invalidStopReturn = returnAddress;
				events->invalidStopSequence = event.sequence;
				events->invalidStopRamHash = _eventRamDiagnostic(events, events->players.diagnostics ? "BEFORE" : NULL);
				events->invalidStopEvent = event;
				events->invalidStopDeferred = true;
				events->invalidStopRejectedHeader = false;
			}
		}
		if (event.playerId >= 0) {
			events->parentSequence = event.sequence;
			events->parentEventCycle = currentCycle;
			events->parentSongId = event.songId;
			events->parentFunctionAddress = function->address;
			events->parentPlayerId = event.playerId;
		}
	} else {
		event.playerId = _playerFromPointer(events, arg0);
		if (function->type == GBA_MP2K_FADE_OUT) {
			event.fadeSpeed = arg1 & 0xFFFF;
			if (!arg0 && _checkedReservedNullFade(events, pc)) {
				events->invalidPlayerAuxiliaryStop = false;
				events->invalidPlayerHeaderFade = false;
				events->invalidPlayerPC = pc;
				events->invalidPlayerComparePC = pc + 10;
				events->invalidPlayerExitPC = pc + 24;
				events->invalidPlayerReturn = returnAddress;
				events->invalidPlayerEvent = event;
				events->invalidPlayerRejectedMagic = false;
				events->invalidPlayerDeferred = true;
				events->invalidPlayerRamHash = _eventRamDiagnostic(events, events->players.diagnostics ? "BEFORE_NULL_FADE" : NULL);
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K RESERVED NULL FADE] detailed_reason=RESERVED_NULL_PLAYER game=%s sequence=%llu r0=%08x r1=%08x pc=%08x return=%08x generation=%u",
					events->profile->gameCode, (unsigned long long) event.sequence, arg0, arg1, pc, returnAddress, events->players.generation);
			}
			int witness = event.playerId < 0 && arg0 ? _knownHeaderFadeWitness(events, pc, arg0) : -1;
			if (witness >= 0) {
				events->invalidPlayerAuxiliaryStop = false;
				events->invalidPlayerHeaderFade = true;
				events->invalidPlayerPC = pc;
				events->invalidPlayerComparePC = pc + 10;
				events->invalidPlayerExitPC = pc + 24;
				events->invalidPlayerReturn = returnAddress;
				events->invalidPlayerEvent = event;
				events->invalidPlayerRejectedMagic = false;
				events->invalidPlayerDeferred = true;
				events->invalidPlayerRamHash = _eventRamDiagnostic(events, events->players.diagnostics ? "BEFORE_ROM_HEADER_FADE" : NULL);
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K ROM HEADER FADE] detailed_reason=KNOWN_ROM_SONG_HEADER game=%s sequence=%llu witness=%d song=%d r0=%08x r1=%08x pc=%08x return=%08x generation=%u",
					events->profile->gameCode, (unsigned long long) event.sequence, witness, _songFromHeader(events, arg0), arg0, arg1, pc, returnAddress, events->players.generation);
			}
		}
		if (function->type == GBA_MP2K_MPLAY_STOP && !arg0 && _checkedReservedNullStop(events, pc) &&
		    _reservedNullTableSlot(events, cpu->gprs[5]) >= 0) {
			events->invalidPlayerAuxiliaryStop = false;
			events->invalidPlayerHeaderFade = false;
			events->invalidPlayerPC = pc;
			events->invalidPlayerComparePC = pc + 8;
			events->invalidPlayerExitPC = pc + 54;
			events->invalidPlayerReturn = returnAddress;
			events->invalidPlayerEvent = event;
			events->invalidPlayerRejectedMagic = false;
			events->invalidPlayerDeferred = true;
			events->invalidPlayerRamHash = _eventRamDiagnostic(events, events->players.diagnostics ? "BEFORE_NULL_STOP" : NULL);
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K RESERVED NULL STOP] detailed_reason=RESERVED_NULL_TABLE_ENTRY game=%s sequence=%llu r0=%08x r1=%08x r4=%08x r5=%08x pc=%08x return=%08x generation=%u reservedSlot=%d",
				events->profile->gameCode, (unsigned long long) event.sequence, arg0, arg1, cpu->gprs[4], cpu->gprs[5], pc, returnAddress, events->players.generation, _reservedNullTableSlot(events, cpu->gprs[5]));
		}
		if (function->type == GBA_MP2K_MPLAY_STOP && event.playerId < 0 && arg0) {
			uint32_t tracks = 0;
			if (_auxiliaryIdleStop(events, pc, returnAddress, arg0, cpu->gprs[5], &tracks)) {
				events->invalidPlayerHeaderFade = false;
				events->invalidPlayerAuxiliaryStop = true;
				events->invalidPlayerAuxiliaryAddress = arg0;
				events->invalidPlayerAuxiliaryTracks = tracks;
				events->invalidPlayerPC = pc;
				events->invalidPlayerComparePC = pc + 8;
				events->invalidPlayerExitPC = pc + 54;
				events->invalidPlayerReturn = returnAddress;
				events->invalidPlayerEvent = event;
				events->invalidPlayerRejectedMagic = events->invalidPlayerAcceptedMagic = false;
				events->invalidPlayerDeferred = true;
				events->invalidPlayerRamHash = _invalidPlayerRamHash(events, events->players.diagnostics ? "BEFORE_AUXILIARY_IDLE_STOP" : NULL);
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K AUXILIARY IDLE STOP] detailed_reason=CHECKED_NATIVE_AUXILIARY_INIT game=%s sequence=%llu r0=%08x r5=%08x tracks=%08x pc=%08x return=%08x generation=%u activation=0",
					events->profile->gameCode, (unsigned long long) event.sequence, arg0, cpu->gprs[5], tracks, pc, returnAddress, events->players.generation);
			}
		}
		if (event.playerId >= 0 &&
		    (function->type == GBA_MP2K_MPLAY_START ||
		     function->type == GBA_MP2K_MPLAY_STOP || function->type == GBA_MP2K_FADE_OUT ||
		     function->type == GBA_MP2K_MPLAY_CONTINUE)) {
			const struct GBAMP2kMusicPlayerInfo* player = GBAMP2kPlayerAt(gba, events->profile, event.playerId);
			if (player) event.playerGuardPassed = player->magic == MP2K_MAGIC;
			if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER)
				event.playerGuardPassed = GBAMP2kPlayerRefresh(&events->players, gba, events->profile, event.playerId) == 1;
			if (!event.playerGuardPassed && function->type == GBA_MP2K_MPLAY_START &&
			    GBAMP2kPlayerCanWatchFinishedRestart(&events->players, gba, events->profile, event.playerId, arg1)) {
				/* The native Start has not executed yet. A proven finished
				 * Continue state only arms observation; Confirm still requires
				 * the accepted header and an actual native first track advance. */
				event.playerGuardPassed = true;
				mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_FINISHED_RESTART_PENDING] game=%s generation=%u player=%d r0=%08x r1=%08x status=%08x clock=%u tempo=%u/%u reason=ACTUAL_CONTINUE_SAME_HEADER_NATIVE_START",
					events->profile->gameCode, events->players.generation, event.playerId, arg0, arg1,
					player->status, player->clock, player->tempoI, player->tempoC);
			}
			if (event.playerGuardPassed && function->type == GBA_MP2K_MPLAY_CONTINUE &&
			    events->profile->playerBacking == GBA_MP2K_RAM_PLAYER)
				GBAMP2kPlayerNoteContinue(&events->players, gba, events->profile, event.playerId);
		}
		if (event.playerId >= 0) {
			event.songId = events->activeSong[event.playerId];
		}
		if (function->type == GBA_MP2K_MPLAY_START) {
			int song = _songFromHeader(events, arg1);
			if (!events->profile->known) event.songId = -1;
			if (song >= 0 && (_read16(rom, events->profile->songTableOffset + song * 8 + 4) == (unsigned) event.playerId || events->profile->known || events->profile->playerBacking == GBA_MP2K_RAM_PLAYER)) {
				event.songId = song;
			}
		}
		if (events->parentSequence && event.playerId == events->parentPlayerId &&
		    _parentCall(events, returnAddress)) {
			event.parentSequence = events->parentSequence;
			if (function->type == GBA_MP2K_MPLAY_START && arg1 == events->parentSongHeader) {
				event.songId = events->parentSongId;
			}
		}
		if (function->type == GBA_MP2K_MPLAY_START && event.playerId >= 0 && event.songId >= 0 &&
		    events->profile->playerBacking != GBA_MP2K_RAM_PLAYER) {
			events->activeSong[event.playerId] = event.songId;
		}
	}
	int ff = event.fastForwardKnown ? (int) event.fastForwardState : -1;
	if (events->invalidStopDeferred && event.sequence == events->invalidStopSequence) return;
	mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K RAW] #%llu %s pc=%08X r0=%08X r1=%08X lr=%08X cycle=%llu ff=%d x=%.2f",
		(unsigned long long) event.sequence, function->name, pc, arg0, arg1, returnAddress,
		(unsigned long long) event.gbaCycle, ff, event.fastForwardRatio);
	if (function->type == GBA_MP2K_MPLAY_STOP || function->type == GBA_MP2K_FADE_OUT) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K GUARD] #%llu %s player=%d accepted=%d",
			(unsigned long long) event.sequence, _typeName(function->type),
			event.playerId, (int) event.playerGuardPassed);
	}
	if (event.frontendStateAvailable && event.pcmSampleKnown) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K EVENT] #%llu %s s=%d p=%d cy=%llu pcm=%llu ff=%d rate=%.3f par=%llu src=%c",
			(unsigned long long) event.sequence, _typeName(event.type), event.songId, event.playerId,
			(unsigned long long) event.gbaCycle, (unsigned long long) event.pcmSample,
			ff, event.frontend.runRate, (unsigned long long) event.parentSequence,
			function->argument == GBA_MP2K_ARG_SONG ? 'C' : 'P');
	} else if (event.frontendStateAvailable) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K EVENT] #%llu %s s=%d p=%d cy=%llu fr=libretro ff=%d rate=%.3f mode=%s par=%llu src=%c",
			(unsigned long long) event.sequence, _typeName(event.type), event.songId, event.playerId,
			(unsigned long long) event.gbaCycle, ff, event.frontend.runRate,
			_throttleName(event.frontend.throttleMode), (unsigned long long) event.parentSequence,
			function->argument == GBA_MP2K_ARG_SONG ? 'C' : 'P');
	} else {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K EVENT] #%llu %s song=%d player=%d pc=%08X cycle=%llu ff=%d x=%.2f parent=%llu src=%c",
			(unsigned long long) event.sequence, _typeName(event.type), event.songId, event.playerId,
			event.functionAddress, (unsigned long long) event.gbaCycle, ff,
			event.fastForwardRatio, (unsigned long long) event.parentSequence,
			function->argument == GBA_MP2K_ARG_SONG ? 'C' : 'P');
	}
	if (event.audioSampleTimestampKnown) {
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K CLOCK EVENT] #%llu %s song=%d player=%d cycle=%llu audioSample=%llu parent=%llu",
			(unsigned long long) event.sequence, _typeName(event.type), event.songId, event.playerId,
			(unsigned long long) event.gbaCycle, (unsigned long long) event.audioSampleTimestamp,
			(unsigned long long) event.parentSequence);
	}
	if (function->type == GBA_MP2K_MPLAY_STOP && event.playerId >= 0 && event.playerGuardPassed) {
		events->activeSong[event.playerId] = -1;
	}
	if (events->invalidPlayerDeferred && event.sequence == events->invalidPlayerEvent.sequence) return;
	if (function->type == GBA_MP2K_MPLAY_START && event.playerId >= 0 && event.songId >= 0 &&
	    (events->profile->playerBacking != GBA_MP2K_RAM_PLAYER || event.playerGuardPassed) &&
	    ((events->profile->playerBacking == GBA_MP2K_RAM_PLAYER && _romPointer(gba, arg1, 8)) ||
	     (_romPointer(gba, arg1, 12) && rom[arg1 - 0x08000000]))) {
		if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER) {
			event.parentSequence = 0; /* Actual r0, never static default routing. */
			events->ramStarts[event.playerId] = event;
			events->ramFirstTickCycle[event.playerId] = 0;
			events->ramStartReturnPC[event.playerId] = returnAddress;
		}
		events->firstTickWatch[event.playerId] = true;
		events->firstTickArmed[event.playerId] = false;
		events->firstTickInitialTrack[event.playerId] = rom[arg1 - 0x08000000] ? _read32(rom, arg1 - 0x08000000 + 8) : 0;
		events->firstTickSong[event.playerId] = event.songId;
		events->firstTickEventCycle[event.playerId] =
			event.parentSequence ? events->parentEventCycle : currentCycle;
		events->firstTickSemanticSequence[event.playerId] =
			event.parentSequence ? event.parentSequence : event.sequence;
	}
	if (events->nativeTickTrace && event.type == GBA_MP2K_MPLAY_START &&
	    event.songId == 342 && event.playerId == 4) {
		events->nativeTickWatch = true;
		events->nativeTickWatchUntil = currentCycle + 1000000;
		events->nativeTickLastTrack = 0;
		events->nativeTickLastClock = UINT32_MAX;
		events->nativeTickLastTempoC = UINT16_MAX;
		events->nativeTickChanges = 0;
	}
	if (events->profile->playerBacking == GBA_MP2K_RAM_PLAYER &&
	    (function->type == GBA_MP2K_SONG_START || function->type == GBA_MP2K_MPLAY_START)) {
		if (function->type == GBA_MP2K_MPLAY_START &&
		    (event.playerId < 0 || event.songId < 0 || !event.playerGuardPassed) && events->sink) {
			/* An unrecognized native start must not leave stale candidate PCM
			 * playing. Stay native until a fully accepted start clears mismatch. */
			if (event.playerId >= 0) events->firstTickWatch[event.playerId] = false;
			events->players.mismatch = true;
			events->sink(&event, events->sinkContext);
		}
		if (events->players.diagnostics && function->type == GBA_MP2K_MPLAY_START)
			mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_%s] game=%s generation=%u player=%d r0=%08x r1=%08x song=%d", event.playerId < 0 || event.songId < 0 || !event.playerGuardPassed ? "MISMATCH" : "START_PENDING", events->profile->gameCode, events->players.generation, event.playerId, arg0, arg1, event.songId);
		return;
	}
	if (events->sink) {
		events->sink(&event, events->sinkContext);
	}
}
