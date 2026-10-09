/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_MP2K_OWNERSHIP_H
#define GBA_MP2K_OWNERSHIP_H

#include <mgba-util/common.h>
CXX_GUARD_START

struct GBAMP2kProfile;

/* Evidence collected at a PSG register write. Unknown writes must remain
 * in the native mix when candidate routing is enabled. */
struct GBAMP2kPsgWriteOwner {
	uint32_t sourcePC;
	bool dma;
	uint8_t channel;
	uint8_t voiceType;
	uint8_t voiceStatus;
	uint32_t activeTrack;
	uint32_t playerTrackBase;
	uint32_t playerTrackStride;
	uint32_t playerTrackCount;
};

bool GBAMP2kIsOwnedPsgWrite(const struct GBAMP2kProfile* profile,
	const struct GBAMP2kPsgWriteOwner* write);
bool GBAMP2kPsgWriteNeedsNative(const struct GBAMP2kProfile* profile,
	const struct GBAMP2kPsgWriteOwner* write, bool trigger);
bool GBAMP2kIsOwnedDirectSoundDma(const struct GBAMP2kProfile* profile,
	uint32_t sourcePC, bool performingDMA, unsigned channel, uint32_t source);
CXX_GUARD_END
#endif
