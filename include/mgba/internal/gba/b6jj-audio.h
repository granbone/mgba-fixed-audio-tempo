/* SPDX-License-Identifier: MPL-2.0 */
#ifndef GBA_B6JJ_AUDIO_H
#define GBA_B6JJ_AUDIO_H

#include <mgba-util/common.h>
CXX_GUARD_START
struct mCore;
struct mAudioClock;
struct B6JJAudio;
struct B6JJAudioEventObservation {
	uint64_t sequence, gameCycle, targetCycle, actualCycle;
	uint32_t id, header;
	bool se;
	uint64_t audioSample;
};
struct B6JJAudioStats {
	uint64_t recoveryCycles;
	uint64_t roots, firstRoot, lastRoot, events, applied, bgm, se, maximumLate;
	uint64_t samples, underrun, overrun, guardFailures;
	uint64_t auxCycle, nextRoot;
	uint64_t maximumRootLate;
	size_t queue, queuePeak, bufferPeak, available, lookaheadSamples;
	const char* failure;
};
/* Experimental Windows prototype. No MP2K profile or state ABI is involved.
 * One primary and one private native board; all calls remain serialized. */
bool B6JJAudioIdentity(const struct mCore* primary);
struct B6JJAudio* B6JJAudioCreate(struct mCore* primary, const struct mAudioClock* clock);
/* Clone the complete loaded native board in memory; never restart a song. */
struct B6JJAudio* B6JJAudioCreateLoaded(struct mCore* primary, const struct mAudioClock* clock);
void B6JJAudioDestroy(struct B6JJAudio* audio);
bool B6JJAudioRender(struct B6JJAudio* audio, int16_t* output, size_t frames);
const struct B6JJAudioStats* B6JJAudioGetStats(const struct B6JJAudio* audio);
/* Read-only diagnostic access; never used to transplant primary state. */
struct mCore* B6JJAudioDiagnosticCore(struct B6JJAudio* audio);
void B6JJAudioSetDiagnosticSink(struct B6JJAudio* audio,
	void (*sink)(void*, const struct B6JJAudioEventObservation*), void* context);
CXX_GUARD_END
#endif
