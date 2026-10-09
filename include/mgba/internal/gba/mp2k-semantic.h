/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_MP2K_SEMANTIC_H
#define GBA_MP2K_SEMANTIC_H
#include <mgba/core/audio-clock.h>
#include <mgba/internal/gba/mp2k-events.h>
CXX_GUARD_START
#define GBA_MP2K_SEMANTIC_QUEUE_CAPACITY 512
struct GBAMP2kSemanticQueue {
	struct mAudioClockEvent events[GBA_MP2K_SEMANTIC_QUEUE_CAPACITY];
	size_t size;
	struct mAudioClockEvent pendingStart;
	bool pendingStartValid;
	uint64_t unresolvedStarts;
	struct mAudioClockEvent pendingStop;
	bool pendingStopValid;
	uint64_t unmatchedSongStops;
	uint64_t dropped;
	uint64_t deduplicated;
};
void GBAMP2kSemanticReset(struct GBAMP2kSemanticQueue* queue);
bool GBAMP2kSemanticPushRaw(struct GBAMP2kSemanticQueue* queue, const struct GBAMP2kEvent* raw,
	struct mAudioClockEvent* emitted);
bool GBAMP2kSemanticPopDue(struct GBAMP2kSemanticQueue* queue, uint64_t sample,
	struct mAudioClockEvent* event);
CXX_GUARD_END
#endif
