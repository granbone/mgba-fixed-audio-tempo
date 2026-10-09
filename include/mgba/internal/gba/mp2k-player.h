/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_MP2K_PLAYER_H
#define GBA_MP2K_PLAYER_H
#include <mgba-util/common.h>
CXX_GUARD_START

struct GBA;
struct mCore;
struct GBAMP2kProfile;
struct GBAMP2kMusicPlayerInfo;
enum GBAMP2kPlayerRegion { GBA_MP2K_PLAYER_NONE, GBA_MP2K_PLAYER_EWRAM, GBA_MP2K_PLAYER_IWRAM };
enum GBAMP2kPlayerState {
	GBA_MP2K_PLAYER_WAIT_INIT, GBA_MP2K_PLAYER_DISCOVERED,
	GBA_MP2K_PLAYER_START_CONFIRMED, GBA_MP2K_PLAYER_ARMED,
	GBA_MP2K_PLAYER_ACTIVE, GBA_MP2K_PLAYER_REJECTED
};
struct GBAMP2kRuntimePlayer {
	uint32_t address, tableEntry, generation, tracks, lastHeader;
	unsigned index, maxTracks, trackCount;
	enum GBAMP2kPlayerRegion region;
	enum GBAMP2kPlayerState state;
	bool initialized, startConfirmed, reserved, unused;
	bool finishedContinuePending;
	uint32_t finishedContinueHeader;
	int lastSong;
};
struct GBAMP2kPlayerRegistry {
	uint32_t generation;
	bool diagnostics, invalidated, mismatch;
	struct GBAMP2kRuntimePlayer slots[32];
};
/* Checked physical RAM, never mirrored addresses or host pointers. */
const void* GBAMP2kPlayerRam(const struct GBA*, uint32_t address, size_t bytes);
const struct GBAMP2kMusicPlayerInfo* GBAMP2kPlayerAt(const struct GBA*, const struct GBAMP2kProfile*, unsigned index);
void GBAMP2kPlayersInit(struct GBAMP2kPlayerRegistry*, const struct GBA*, const struct GBAMP2kProfile*, uint32_t generation);
/* 0: initialization/busy; 1: validated initialized slot; -1: invalid live state. */
int GBAMP2kPlayerRefresh(struct GBAMP2kPlayerRegistry*, const struct GBA*, const struct GBAMP2kProfile*, unsigned index);
bool GBAMP2kPlayerConfirm(struct GBAMP2kPlayerRegistry*, const struct GBA*, const struct GBAMP2kProfile*, unsigned index, int song, uint32_t header);
void GBAMP2kPlayerNoteContinue(struct GBAMP2kPlayerRegistry*, const struct GBA*, const struct GBAMP2kProfile*, unsigned index);
/* A witnessed Continue of finished tracks may precede the same native Start.
 * Only permits watching its first tick; the old state cannot be Confirmed. */
bool GBAMP2kPlayerCanWatchFinishedRestart(struct GBAMP2kPlayerRegistry*, const struct GBA*, const struct GBAMP2kProfile*, unsigned index, uint32_t header);
void GBAMP2kPlayerSetState(struct GBAMP2kPlayerRegistry*, const struct GBAMP2kProfile*, unsigned, enum GBAMP2kPlayerState);
struct GBAMP2kPlayerRegistry* GBAMP2kEventsPlayers(struct mCore*);

CXX_GUARD_END
#endif
