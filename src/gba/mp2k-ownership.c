/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/mp2k-ownership.h>
#include <mgba/internal/gba/mp2k-profile.h>

bool GBAMP2kIsOwnedPsgWrite(const struct GBAMP2kProfile* profile,
	const struct GBAMP2kPsgWriteOwner* write) {
	if (!profile || !write || write->dma || write->sourcePC < profile->psgCodeStart ||
	    write->sourcePC > profile->psgCodeEnd || write->channel >= 4 ||
	    !(write->voiceStatus & 1) || write->voiceType != write->channel + 1 ||
	    !write->activeTrack || !write->playerTrackStride ||
	    !write->playerTrackCount || write->playerTrackCount > 16 ||
	    write->activeTrack < write->playerTrackBase) {
		return false;
	}
	uint32_t offset = write->activeTrack - write->playerTrackBase;
	return offset % write->playerTrackStride == 0 &&
	    offset / write->playerTrackStride < write->playerTrackCount;
}

bool GBAMP2kPsgWriteNeedsNative(const struct GBAMP2kProfile* profile,
	const struct GBAMP2kPsgWriteOwner* write, bool trigger) {
	if (!write || write->channel >= 4) return true;
	if (GBAMP2kIsOwnedPsgWrite(profile, write)) return false;
	/* A range inferred from function proximity is not proof of PSG ownership. */
	if (profile && !profile->known) return true;
	bool driverPC = profile && write->sourcePC >= profile->driverCodeStart &&
		write->sourcePC < profile->driverCodeEnd;
	return !driverPC || write->dma || (trigger && (write->voiceStatus & 1));
}

bool GBAMP2kIsOwnedDirectSoundDma(const struct GBAMP2kProfile* profile,
	uint32_t sourcePC, bool performingDMA, unsigned channel, uint32_t source) {
	return profile && channel < 4 && profile->fifoSource[channel] && !performingDMA &&
		sourcePC >= profile->driverCodeStart && sourcePC < profile->driverCodeEnd &&
		source == profile->fifoSource[channel];
}
