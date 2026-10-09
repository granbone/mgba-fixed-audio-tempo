/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/mp2k-semantic.h>
#include <string.h>

void GBAMP2kSemanticReset(struct GBAMP2kSemanticQueue* queue) {
	memset(queue, 0, sizeof(*queue));
}

bool GBAMP2kSemanticPushRaw(struct GBAMP2kSemanticQueue* queue, const struct GBAMP2kEvent* raw,
	struct mAudioClockEvent* emitted) {
	if (!raw->audioSampleTimestampKnown || raw->songId < 0 || raw->playerId < 0) {
		return false;
	}
	enum mAudioClockEventType type;
	struct mAudioClockEvent parent = { 0 };
	bool useParent = false;
	switch (raw->type) {
	case GBA_MP2K_SONG_START:
		if (queue->pendingStartValid) {
			++queue->unresolvedStarts;
		}
		queue->pendingStart = (struct mAudioClockEvent) {
			.type = M_AUDIO_CLOCK_PLAY_SONG,
			.songId = raw->songId,
			.playerId = raw->playerId,
			.gameCycle = raw->gbaCycle,
			.sequence = raw->sequence,
			.sourceSequence = raw->sequence,
			.audioSampleTimestamp = raw->audioSampleTimestamp
		};
		queue->pendingStartValid = true;
		return false;
	case GBA_MP2K_MPLAY_START:
		if (queue->pendingStartValid && raw->parentSequence == queue->pendingStart.sequence &&
		    raw->songId == queue->pendingStart.songId && raw->playerId == queue->pendingStart.playerId) {
			parent = queue->pendingStart;
			useParent = true;
			queue->pendingStartValid = false;
			++queue->deduplicated;
		}
		type = M_AUDIO_CLOCK_PLAY_SONG;
		break;
	case GBA_MP2K_SONG_STOP:
		if (queue->pendingStopValid) {
			++queue->unmatchedSongStops;
		}
		queue->pendingStop = (struct mAudioClockEvent) {
			.type = M_AUDIO_CLOCK_STOP_SONG,
			.songId = raw->songId,
			.playerId = raw->playerId,
			.gameCycle = raw->gbaCycle,
			.sequence = raw->sequence,
			.sourceSequence = raw->sequence,
			.audioSampleTimestamp = raw->audioSampleTimestamp
		};
		queue->pendingStopValid = true;
		return false;
	case GBA_MP2K_MPLAY_STOP:
		if (!raw->playerGuardPassed) {
			return false;
		}
		if (queue->pendingStopValid && raw->parentSequence == queue->pendingStop.sequence &&
		    raw->playerId == queue->pendingStop.playerId) {
			parent = queue->pendingStop;
			useParent = true;
			queue->pendingStopValid = false;
			++queue->deduplicated;
		}
		type = M_AUDIO_CLOCK_STOP_SONG;
		break;
	case GBA_MP2K_FADE_OUT:
		if (!raw->playerGuardPassed || !raw->fadeSpeed) {
			return false;
		}
		type = M_AUDIO_CLOCK_FADE_PLAYER;
		break;
	default:
		return false;
	}
	if (queue->size == GBA_MP2K_SEMANTIC_QUEUE_CAPACITY) {
		++queue->dropped;
		return false;
	}
	struct mAudioClockEvent event = useParent ? parent : (struct mAudioClockEvent) {
		.type = type,
		.songId = raw->songId,
		.playerId = raw->playerId,
		.gameCycle = raw->gbaCycle,
		.sequence = raw->sequence,
		.sourceSequence = raw->sequence,
		.audioSampleTimestamp = raw->audioSampleTimestamp
	};
	if (type == M_AUDIO_CLOCK_FADE_PLAYER) {
		event.fadeSpeed = raw->fadeSpeed;
	}
	size_t insert = queue->size++;
	while (insert && queue->events[insert - 1].audioSampleTimestamp > event.audioSampleTimestamp) {
		queue->events[insert] = queue->events[insert - 1];
		--insert;
	}
	queue->events[insert] = event;
	if (emitted) {
		*emitted = event;
	}
	return true;
}

bool GBAMP2kSemanticPopDue(struct GBAMP2kSemanticQueue* queue, uint64_t sample,
	struct mAudioClockEvent* event) {
	if (!queue->size || queue->events[0].audioSampleTimestamp > sample) {
		return false;
	}
	*event = queue->events[0];
	--queue->size;
	memmove(queue->events, queue->events + 1, queue->size * sizeof(*queue->events));
	return true;
}
