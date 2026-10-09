/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/mp2k-player.h>
#include <mgba/internal/gba/mp2k-profile.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/core/log.h>
#include <string.h>
#include <stdlib.h>
mLOG_DECLARE_CATEGORY(GBA_MP2K_EVENTS);
_Static_assert(sizeof(struct GBAMP2kMusicPlayerInfo) == 64, "MP2K player prefix layout");
_Static_assert(sizeof(struct GBAMP2kMusicPlayerTrack) == 80, "MP2K track layout");
static uint32_t _word(const uint8_t* p) { return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24; }
const void* GBAMP2kPlayerRam(const struct GBA* gba, uint32_t address, size_t bytes) {
	if (!gba || (address & 3) || !bytes) return NULL;
	if (address >= 0x02000000 && address < 0x02040000 && bytes <= 0x02040000 - address)
		return (const uint8_t*) gba->memory.wram + address - 0x02000000;
	if (address >= 0x03000000 && address < 0x03008000 && bytes <= 0x03008000 - address)
		return (const uint8_t*) gba->memory.iwram + address - 0x03000000;
	return NULL;
}

const struct GBAMP2kMusicPlayerInfo* GBAMP2kPlayerAt(const struct GBA* gba, const struct GBAMP2kProfile* p, unsigned id) {
	if (!gba || !gba->memory.rom || !p || id >= p->playerCount || id >= 32 || p->playerTableOffset > gba->memory.romSize ||
	    p->playerCount > (gba->memory.romSize - p->playerTableOffset)/12) return NULL;
	uint32_t address = _word((const uint8_t*) gba->memory.rom + p->playerTableOffset + id*12);
	if (p->playerBacking == GBA_MP2K_RAM_PLAYER ? address < 0x02000000 || address >= 0x02040000 :
	    address < 0x03000000 || address >= 0x03008000) return NULL;
	return GBAMP2kPlayerRam(gba, address, sizeof(struct GBAMP2kMusicPlayerInfo));
}

void GBAMP2kPlayerSetState(struct GBAMP2kPlayerRegistry* r, const struct GBAMP2kProfile* p, unsigned id, enum GBAMP2kPlayerState state) {
	if (!r || !p || id >= p->playerCount || id >= 32 || state > GBA_MP2K_PLAYER_REJECTED) return;
	static const char* names[] = {"WAIT_INIT", "DISCOVERED", "START", "ARMED", "ACTIVE", "REJECTED"};
	struct GBAMP2kRuntimePlayer* s = &r->slots[id];
	if (r->diagnostics && s->state != state)
		mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_%s] game=%s profile=%s generation=%u player=%u address=%08x tracks=%08x song=%d header=%08x", names[state], p->gameCode,p->name,r->generation,id,s->address,s->tracks,s->lastSong,s->lastHeader);
	s->state = state;
}

void GBAMP2kPlayersInit(struct GBAMP2kPlayerRegistry* r, const struct GBA* gba, const struct GBAMP2kProfile* p, uint32_t generation) {
	memset(r,0,sizeof(*r));
	r->generation = generation ? generation : 1;
	const char* diag = getenv("MGBA_FIXED_AUDIO_DIAGNOSTICS");
	r->diagnostics = diag && !strcmp(diag,"1");
	if (!p || p->playerBacking != GBA_MP2K_RAM_PLAYER || !GBAMP2kProfileValidate(p, gba)) return;
	if (r->diagnostics) mLOG(GBA_MP2K_EVENTS, INFO,"[RAM_PLAYER_GENERATION_CHANGE] game=%s profile=%s generation=%u",p->gameCode,p->name,r->generation);
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	for (unsigned id=0;id<p->playerCount;++id) {
		struct GBAMP2kRuntimePlayer* s = &r->slots[id];
		s->index=id;
		s->generation=r->generation;
		s->tableEntry=p->playerTableOffset+id*12;
		s->address=_word(rom+s->tableEntry);
		s->tracks=_word(rom+s->tableEntry+4);
		s->maxTracks=rom[s->tableEntry+8] | rom[s->tableEntry+9]<<8;
		s->lastSong=-1;
		s->unused=true;
		s->reserved=!s->address && !s->tracks && !_word(rom+s->tableEntry+8);
		s->region=s->reserved?GBA_MP2K_PLAYER_NONE:s->address<0x03000000?GBA_MP2K_PLAYER_EWRAM:GBA_MP2K_PLAYER_IWRAM;
		if (r->diagnostics) mLOG(GBA_MP2K_EVENTS,INFO,"[RAM_PLAYER_%s] game=%s generation=%u player=%u address=%08x tracks=%08x maxTracks=%u",s->reserved?"RESERVED":"WAIT_INIT",p->gameCode,r->generation,id,s->address,s->tracks,s->maxTracks);
	}
}
/* Some drivers Continue every slot, including a zero-track song used to stop
 * unused players. Until native MPlayMain sets the pause bit again, that idle
 * object retains tempoC == 150. It has no renderer position to reconstruct. */
static bool _emptySongIdle(const struct GBA* gba, const struct GBAMP2kProfile* p,
                          const struct GBAMP2kRuntimePlayer* s, const struct GBAMP2kMusicPlayerInfo* n) {
	if (n->status || n->tempoC != 150 || n->songHeader < 0x08000000 ||
	    gba->memory.romSize < 8 || n->songHeader - 0x08000000 > gba->memory.romSize - 8 ||
	    p->songTableOffset > gba->memory.romSize || p->songCount > (gba->memory.romSize - p->songTableOffset) / 8) return false;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	if (rom[n->songHeader - 0x08000000]) return false;
	bool known = false;
	for (unsigned song = 0; song < p->songCount; ++song)
		if (_word(rom + p->songTableOffset + song * 8) == n->songHeader) { known = true; break; }
	if (!known) return false;
	const struct GBAMP2kMusicPlayerTrack* tracks = GBAMP2kPlayerRam(gba, n->tracks, s->maxTracks * sizeof(*tracks));
	if (!tracks) return false;
	for (unsigned track = 0; track < s->maxTracks; ++track)
		if (tracks[track].flags) return false;
	return true;
}

/* Native All-Continue clears pause even for a finished nonempty song. Its
 * old counter need not be a normalized active remainder. Require the actual
 * Continue, a known header/tone, and every allocated track entirely cleared;
 * never use this as evidence of playback or accept an active counter overflow. */
static bool _finishedTracks(const struct GBA* gba, const struct GBAMP2kProfile* p,
                           const struct GBAMP2kRuntimePlayer* s, const struct GBAMP2kMusicPlayerInfo* n) {
	if (!n->clock || !n->tempoI || n->trackCount != s->maxTracks ||
	    p->songTableOffset > gba->memory.romSize || p->songCount > (gba->memory.romSize - p->songTableOffset) / 8 ||
	    n->songHeader < 0x08000000 || gba->memory.romSize < 8 ||
	    n->songHeader - 0x08000000 > gba->memory.romSize - 8) return false;
	const uint8_t* rom = (const void*) gba->memory.rom;
	const uint8_t* header = rom + n->songHeader - 0x08000000;
	if (!header[0] || header[0] > s->maxTracks || n->tone != _word(header + 4)) return false;
	bool known = false;
	for (unsigned song = 0; song < p->songCount; ++song)
		if (_word(rom + p->songTableOffset + song * 8) == n->songHeader) { known = true; break; }
	if (!known) return false;
	const struct GBAMP2kMusicPlayerTrack* tracks = GBAMP2kPlayerRam(gba, n->tracks, s->maxTracks * sizeof(*tracks));
	if (!tracks) return false;
	for (unsigned track = 0; track < s->maxTracks; ++track)
		if (tracks[track].flags) return false;
	return true;
}

void GBAMP2kPlayerNoteContinue(struct GBAMP2kPlayerRegistry* r, const struct GBA* gba,
                             const struct GBAMP2kProfile* p, unsigned id) {
	if (!r || !p || id >= p->playerCount || id >= 32 || GBAMP2kPlayerRefresh(r, gba, p, id) != 1) return;
	const struct GBAMP2kMusicPlayerInfo* n = GBAMP2kPlayerAt(gba, p, id);
	struct GBAMP2kRuntimePlayer* s = &r->slots[id];
	if (!n || n->status != 0x80000000U || !_finishedTracks(gba, p, s, n)) return;
	s->finishedContinuePending = true;
	s->finishedContinueHeader = n->songHeader;
	if (r->diagnostics) mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_FINISHED_CONTINUE] game=%s generation=%u player=%u address=%08x header=%08x clock=%u tempo=%u/%u reason=ACTUAL_CONTINUE_ALL_TRACKS_FINISHED",
		p->gameCode, r->generation, id, s->address, n->songHeader, n->clock, n->tempoI, n->tempoC);
}

int GBAMP2kPlayerRefresh(struct GBAMP2kPlayerRegistry* r, const struct GBA* gba, const struct GBAMP2kProfile* p, unsigned id) {
	if (!r || !p || id>=p->playerCount || id>=32) return -1;
	struct GBAMP2kRuntimePlayer* s = &r->slots[id];
	if (s->reserved) return 0;
	const struct GBAMP2kMusicPlayerInfo* n = GBAMP2kPlayerAt(gba,p,id);
	if (!n || s->generation != r->generation) return -1;
	if (!s->initialized && n->magic != MP2K_MAGIC) return 0;
	if (!n->magic) {
		/* SoundInit replacing a previously observed object invalidates every
			* dependent renderer/mapping. First initialization simply keeps waiting. */
		if (s->initialized) r->invalidated = true;
		return 0;
	}
	if (n->magic == MP2K_MAGIC+1) return 0;
	/* MPlayStop can clear the header before a following MPlayStart. This
	 * is a normal lifecycle transition, not evidence of SoundInit. */
	if (n->magic != MP2K_MAGIC || n->tracks != s->tracks || !s->maxTracks || s->maxTracks>16 || n->trackCount>s->maxTracks ||
	    !GBAMP2kPlayerRam(gba,n->tracks,s->maxTracks*sizeof(struct GBAMP2kMusicPlayerTrack)) ||
	    (n->status & 0x7FFF0000U) || n->tempoI>1024) {
		if (r->diagnostics) mLOG(GBA_MP2K_EVENTS, WARN, "[RAM_PLAYER_REJECTED] player=%u magic=%08x tracks=%08x/%08x count=%u/%u status=%08x tempo=%u/%u header=%08x",id,n->magic,n->tracks,s->tracks,n->trackCount,s->maxTracks,n->status,n->tempoI,n->tempoC,n->songHeader);
		GBAMP2kPlayerSetState(r,p,id,GBA_MP2K_PLAYER_REJECTED);
		return -1;
	}
	if (!(n->status & 0x80000000U) && n->songHeader && n->tempoC >= 150) {
		if (_emptySongIdle(gba, p, s, n)) {
			if (r->diagnostics) mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_EMPTY_SONG_IDLE] game=%s player=%u address=%08x header=%08x status=%08x tempo=%u/%u clock=%u tracks=%08x reason=ZERO_TRACK_SONG_ALL_CONTINUE", p->gameCode, id, s->address, n->songHeader, n->status, n->tempoI, n->tempoC, n->clock, n->tracks);
			return 0;
		}
		if (!n->status && s->finishedContinuePending && s->finishedContinueHeader == n->songHeader &&
		    _finishedTracks(gba, p, s, n)) {
			if (r->diagnostics) mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_FINISHED_SONG_IDLE] game=%s generation=%u player=%u address=%08x header=%08x clock=%u tempo=%u/%u reason=NATIVE_CONTINUE_NO_ACTIVE_TRACKS",
				p->gameCode, r->generation, id, s->address, n->songHeader, n->clock, n->tempoI, n->tempoC);
			return 0;
		}
		if (r->diagnostics) mLOG(GBA_MP2K_EVENTS, WARN, "[RAM_PLAYER_REJECTED] player=%u address=%08x header=%08x status=%08x tempo=%u/%u detailed_reason=ACTIVE_TEMPO_COUNTER_INVALID", id, s->address, n->songHeader, n->status, n->tempoI, n->tempoC);
		GBAMP2kPlayerSetState(r, p, id, GBA_MP2K_PLAYER_REJECTED);
		return -1;
	}
	s->finishedContinuePending = false;
	s->trackCount=n->trackCount;
	if (!s->initialized) {
		s->initialized=true;
		GBAMP2kPlayerSetState(r,p,id,GBA_MP2K_PLAYER_DISCOVERED);
	}
	return 1;
}

bool GBAMP2kPlayerCanWatchFinishedRestart(struct GBAMP2kPlayerRegistry* r, const struct GBA* gba,
                                         const struct GBAMP2kProfile* p, unsigned id, uint32_t header) {
	if (!r || !p || p->playerBacking != GBA_MP2K_RAM_PLAYER || id >= p->playerCount || id >= 32 ||
	    GBAMP2kPlayerRefresh(r, gba, p, id) != 0) return false;
	const struct GBAMP2kRuntimePlayer* s = &r->slots[id];
	const struct GBAMP2kMusicPlayerInfo* n = GBAMP2kPlayerAt(gba, p, id);
	return n && s->initialized && !s->reserved && s->finishedContinuePending &&
		s->finishedContinueHeader == header && n->songHeader == header && !n->status &&
		n->magic == MP2K_MAGIC && _finishedTracks(gba, p, s, n);
}

bool GBAMP2kPlayerConfirm(struct GBAMP2kPlayerRegistry* r, const struct GBA* gba, const struct GBAMP2kProfile* p, unsigned id, int song, uint32_t header) {
	if (!gba || !p || p->songTableOffset > gba->memory.romSize ||
	    p->songCount > (gba->memory.romSize-p->songTableOffset)/8 || song<0 || (unsigned)song>=p->songCount || GBAMP2kPlayerRefresh(r,gba,p,id)!=1) return false;
	const struct GBAMP2kMusicPlayerInfo* n = GBAMP2kPlayerAt(gba,p,id);
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	if (_word(rom+p->songTableOffset+song*8)!=header || header<0x08000000 || header-0x08000000>gba->memory.romSize-8 ||
	    n->songHeader!=header || !n->tempoI || (n->status&0x80000000U) || rom[header-0x08000000]>r->slots[id].maxTracks ||
	    n->tone != _word(rom+header-0x08000000+4)) return false;
	struct GBAMP2kRuntimePlayer* s = &r->slots[id];
	s->lastSong=song;
	s->lastHeader=header;
	s->startConfirmed=true;
	s->unused=false;
	r->mismatch=false;
	GBAMP2kPlayerSetState(r,p,id,GBA_MP2K_PLAYER_START_CONFIRMED);
	if (r->diagnostics) mLOG(GBA_MP2K_EVENTS,INFO,"[RAM_PLAYER_MATCH] game=%s generation=%u player=%u r0=%08x r1=%08x song=%d priority=%u",p->gameCode,r->generation,id,s->address,header,song,n->priority);
	return true;
}
