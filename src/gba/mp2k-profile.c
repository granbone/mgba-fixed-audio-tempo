/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/mp2k-profile.h>

#include <mgba/internal/gba/gba.h>
#include <string.h>
#include <stdlib.h>

static const struct GBAMP2kFunction _aamjFunctions[] = {
	{ 0x080BFF80, 0, "m4aMPlayContinue", GBA_MP2K_MPLAY_CONTINUE, GBA_MP2K_ARG_PLAYER, 0x1C02 },
	{ 0x080BFF9C, 0, "m4aMPlayFadeOut", GBA_MP2K_FADE_OUT, GBA_MP2K_ARG_PLAYER, 0x1C02 },
	{ 0x080C0058, 0x080C007C, "m4aSongNumStart", GBA_MP2K_SONG_START, GBA_MP2K_ARG_SONG, 0xB500 },
	{ 0x080C0084, 0x080C00D0, "m4aSongNumStartOrChange", GBA_MP2K_SONG_START, GBA_MP2K_ARG_SONG, 0xB500 },
	{ 0x080C00D0, 0x080C0124, "m4aSongNumStartOrContinue", GBA_MP2K_SONG_START, GBA_MP2K_ARG_SONG, 0xB500 },
	{ 0x080C0124, 0x080C014E, "m4aSongNumStop", GBA_MP2K_SONG_STOP, GBA_MP2K_ARG_SONG, 0xB500 },
	{ 0x080C0158, 0x080C0182, "m4aSongNumContinue", GBA_MP2K_MPLAY_CONTINUE, GBA_MP2K_ARG_SONG, 0xB500 },
	{ 0x080C06D4, 0, "m4aMPlayStart", GBA_MP2K_MPLAY_START, GBA_MP2K_ARG_PLAYER, 0xB5F0 },
	{ 0x080C0788, 0, "m4aMPlayStop", GBA_MP2K_MPLAY_STOP, GBA_MP2K_ARG_PLAYER, 0xB570 }
};

static const struct GBAMP2kProfile _aamjProfile = {
	.name = "AAMJ-known", .gameCode = "AAMJ", .romCrc32 = 0xF3E41D73,
	.romSize = 0x800000, .songTableOffset = 0x108A3C, .songCount = 455,
	.playerTableOffset = 0x1089D0, .playerCount = 9, .soundMode = 0x0083FC00,
	.soundInfoPointerOffset = 0xC0030,
	.outputSampleRate = 32768, .lookaheadSamples = 202,
	.driverCodeStart = 0x080BF000, .driverCodeEnd = 0x080C1500,
	.psgCodeStart = 0x080C0A70, .psgCodeEnd = 0x080C0E88,
	.firstTickCodeEnd = 0x080BFF80,
	.fifoSource = { 0, 0x030033E0, 0x03003A10, 0 },
	.functions = _aamjFunctions,
	.functionCount = sizeof(_aamjFunctions) / sizeof(*_aamjFunctions),
	.known = true
};

static uint16_t _read16(const uint8_t* data, size_t offset) {
	return data[offset] | data[offset + 1] << 8;
}

static uint32_t _read32(const uint8_t* data, size_t offset) {
	return (uint32_t) _read16(data, offset) | (uint32_t) _read16(data, offset + 2) << 16;
}

static bool _romPointer(const struct GBA* gba, uint32_t address, size_t length) {
	if (address < 0x08000000 || address >= 0x08000000 + gba->memory.romSize) return false;
	return length <= gba->memory.romSize - (address - 0x08000000);
}

static bool _ramPointer(uint32_t address) {
	return (address >= 0x02000000 && address < 0x02040000) ||
		(address >= 0x03000000 && address < 0x03008000);
}

/* Individually reviewed identities only. This grants eligibility, not playback:
 * scanner tables, code hooks and live RAM must still validate independently. */
static bool _ramPlayerTrial(const struct GBA* gba) {
	static const struct { uint32_t crc, size; const char* code; } trials[] = {
		{ 0x58A972DF, 0x400000, "AAKJ" }, { 0x5A2CADA1, 0x400000, "B2SJ" },
		{ 0xA57B0034, 0x1000000, "AFXJ" }, { 0xD9516E50, 0x800000, "AREJ" },
		{ 0xD5D43B4B, 0x1000000, "BVEJ" },
		{ 0x41B39214, 0x1000000, "BAXJ" },
		{ 0x587F1AEF, 0x400000, "AJ6J" },
		{ 0x1527CA8C, 0x800000, "BAGJ" },
		{ 0x3FE9C701, 0x1000000, "AK2J" },
		{ 0x98E4F096, 0x800000, "AE2J" },
		{ 0xEDCB7D46, 0x800000, "A2GJ" },
		{ 0xC140FF7A, 0x800000, "AMFJ" },
		{ 0xF0E81971, 0x1000000, "BK5J" },
		{ 0xEE41C0EB, 0x800000, "BBMJ" },
		{ 0x5B8AAA03, 0x800000, "ANFJ" },
		{ 0x7A2C0D61, 0x800000, "AXRJ" },
		{ 0x45C84466, 0x2000000, "BC2J" },
		{ 0xA04BE1ED, 0x1000000, "BVAJ" },
		{ 0x1867CCEF, 0x1000000, "B3TJ" },
		{ 0xB521C635, 0x800000, "A6OJ" },
		{ 0x0C01F3B8, 0x400000, "BBFJ" },
		{ 0x8AF1EC73, 0x800000, "BPPJ" },
		{ 0xCE2B48C4, 0x800000, "AFCJ" },
		{ 0x82B4F3FA, 0x1000000, "AKZJ" },
		{ 0x8D5A0D84, 0x2000000, "B8CJ" },
		{ 0xF2BF8990, 0x400000, "AJ3J" },
		{ 0xD66379E1, 0x800000, "BKBJ" },
		{ 0x09185657, 0x800000, "A4NJ" },
		{ 0xFE1BE6C1, 0x1000000, "B36J" },
		{ 0x830B795F, 0x1000000, "AGFJ" }
	};
	for (size_t i = 0; i < sizeof(trials) / sizeof(*trials); ++i)
		if (gba->romCrc32 == trials[i].crc && gba->memory.romSize == trials[i].size &&
		    !memcmp((const uint8_t*) gba->memory.rom + 0xAC, trials[i].code, 4)) return true;
	return false;
}

const struct GBAMP2kProfile* GBAMP2kKnownProfile(const struct GBA* gba) {
	if (!gba || !gba->memory.rom || gba->memory.romSize != _aamjProfile.romSize ||
	    gba->romCrc32 != _aamjProfile.romCrc32 ||
	    memcmp((const uint8_t*) gba->memory.rom + 0xAC, _aamjProfile.gameCode, 4)) return NULL;
	return &_aamjProfile;
}

bool GBAMP2kConservativeIdentity(const struct GBA* gba) {
	if (!gba || !gba->memory.rom || gba->memory.romSize < 0x200) return false;
	return GBAMP2kKnownProfile(gba) || _ramPlayerTrial(gba) ||
		(gba->romCrc32 == 0xD38763E1 && gba->memory.romSize == 0x800000 &&
		 !memcmp((const uint8_t*) gba->memory.rom + 0xAC, "AFEJ", 4));
}

/* Classify backing only. The full validator below still checks every slot,
 * overlap, track, song, hook and live player before output can activate. */
static bool _runtimeRamBacking(const struct GBA* gba, uint32_t offset,
	unsigned count, bool experimental) {
	if (_ramPlayerTrial(gba)) return true;
	if (!experimental || offset > gba->memory.romSize ||
	    count > (gba->memory.romSize - offset) / 12) return false;
	for (unsigned i = 0; i < count; ++i) {
		uint32_t address = _read32((const uint8_t*) gba->memory.rom, offset + i * 12);
		if (address) return address >= 0x02000000 && address < 0x02040000;
	}
	return false;
}

bool GBAMP2kProfileValidate(const struct GBAMP2kProfile* profile, const struct GBA* gba) {
	if (!profile || !gba || !gba->memory.rom || !profile->songCount || !profile->playerCount ||
	    profile->playerCount > GBA_MP2K_MAX_PLAYERS || profile->songCount > UINT16_MAX ||
	    gba->memory.romSize < 0x200 ||
	    profile->songTableOffset > gba->memory.romSize ||
	    profile->songCount > (gba->memory.romSize - profile->songTableOffset) / 8 ||
	    profile->playerTableOffset > gba->memory.romSize ||
	    profile->playerCount > (gba->memory.romSize - profile->playerTableOffset) / 12 ||
	    (profile->functionCount && !profile->functions) || profile->functionCount > 9) return false;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	for (unsigned i = 0; i < profile->playerCount; ++i) {
		size_t offset = profile->playerTableOffset + i * 12;
		uint32_t player = _read32(rom, offset);
		uint32_t tracks = _read32(rom, offset + 4);
		uint32_t count = _read16(rom, offset + 8);
		/* Some drivers reserve completely empty player slots. Never dereference them. */
		if (!profile->known && !player && !tracks && !_read32(rom, offset + 8)) continue;
		if ((player & 3) || (tracks & 3) || (profile->playerBacking == GBA_MP2K_RAM_PLAYER ?
		     player < 0x02000000 || player > 0x02040000 - sizeof(struct GBAMP2kMusicPlayerInfo) :
		     player < 0x03000000 || player > 0x03008000 - sizeof(struct GBAMP2kMusicPlayerInfo)) ||
		    !_ramPointer(tracks) || !count || count > 16 ||
		    (tracks >= 0x03000000 ? tracks > 0x03008000 - count * sizeof(struct GBAMP2kMusicPlayerTrack) :
		     tracks > 0x02040000 - count * sizeof(struct GBAMP2kMusicPlayerTrack))) return false;
	}
	if (profile->playerBacking == GBA_MP2K_RAM_PLAYER) {
		for (unsigned i = 0; i < profile->playerCount; ++i) {
			uint32_t player = _read32(rom, profile->playerTableOffset + i * 12);
			if (!player) continue;
			for (unsigned j = 0; j < profile->playerCount; ++j) {
				uint32_t other = _read32(rom, profile->playerTableOffset + j * 12);
				uint32_t tracks = _read32(rom, profile->playerTableOffset + j * 12 + 4);
				unsigned bytes = _read16(rom, profile->playerTableOffset + j * 12 + 8) * sizeof(struct GBAMP2kMusicPlayerTrack);
				if ((i != j && other && player < other + sizeof(struct GBAMP2kMusicPlayerInfo) && other < player + sizeof(struct GBAMP2kMusicPlayerInfo)) ||
				    (bytes && player < tracks + bytes && tracks < player + sizeof(struct GBAMP2kMusicPlayerInfo))) return false;
			}
		}
	}
	for (unsigned i = 0; i < profile->songCount; ++i) {
		uint32_t header = _read32(rom, profile->songTableOffset + i * 8);
		uint16_t player = _read16(rom, profile->songTableOffset + i * 8 + 4);
		if (!profile->known && !header && !_read32(rom, profile->songTableOffset + i * 8 + 4)) continue;
		if ((header & 3) || !_romPointer(gba, header, 8) || player >= profile->playerCount) return false;
		size_t offset = header - 0x08000000;
		unsigned tracks = rom[offset];
		if (tracks > 16 || !_romPointer(gba, header, 8 + tracks * 4)) return false;
		if (tracks && !_romPointer(gba, _read32(rom, offset + 4), 1)) return false;
		for (unsigned track = 0; track < tracks; ++track) {
			if (!_romPointer(gba, _read32(rom, offset + 8 + track * 4), 1)) return false;
		}
	}
	for (size_t i = 0; i < profile->functionCount; ++i) {
		const struct GBAMP2kFunction* function = &profile->functions[i];
		if (!_romPointer(gba, function->address, 2) ||
		    _read16(rom, function->address - 0x08000000) != function->firstOpcode) return false;
	}
	return true;
}

const struct GBAMP2kFunction* GBAMP2kProfileFunctionAt(const struct GBAMP2kProfile* profile,
	uint32_t address) {
	if (!profile || !profile->functions || !profile->functionCount ||
	    address < profile->functions[0].address ||
	    address > profile->functions[profile->functionCount - 1].address) return NULL;
	size_t low = 0, high = profile->functionCount;
	while (low < high) {
		size_t mid = low + (high - low) / 2;
		if (profile->functions[mid].address < address) low = mid + 1;
		else high = mid;
	}
	return low < profile->functionCount && profile->functions[low].address == address ?
		&profile->functions[low] : NULL;
}

static uint32_t _thumbCallTarget(const uint8_t* rom, size_t size, size_t start, size_t end,
	uint32_t excludedA, uint32_t excludedB, uint32_t required) {
	uint32_t found = 0;
	if (end > size - 4) end = size - 4;
	for (size_t pos = start; pos <= end; pos += 2) {
		uint16_t first = _read16(rom, pos), second = _read16(rom, pos + 2);
		if ((first & 0xF800) != 0xF000 || (second & 0xF800) != 0xF800) continue;
		int32_t displacement = ((first & 0x7FF) << 12) | ((second & 0x7FF) << 1);
		if (displacement & (1 << 22)) displacement -= 1 << 23;
		uint32_t target = (uint32_t) (pos + 4 + displacement + 0x08000000);
		if (target == excludedA || target == excludedB ||
		    target < 0x08000000 || target >= 0x08000000 + size) continue;
		if (required && target != required) continue;
		if (found && found != target) return 0;
		found = target;
	}
	return found;
}

static uint32_t _fadeNearContinue(const uint8_t* rom, size_t size, uint32_t continuation) {
	/* This compiler variant checks MusicPlayer.magic, stores fade speed in
	 * fadeOI/fadeOC and sets fadeOV to 256. Restrict the search to the already
	 * referenced driver: some ROMs contain several unused copies of m4a. A
	 * signature is only static evidence; the live player guard is still needed. */
	static const uint16_t signature[] = {
		0x1C02, 0x0409, 0x0C09, 0x6B53, 0x4804, 0x4283, 0xD104,
		0x84D1, 0x8491, 0x2080, 0x0040, 0x8510, 0x4770, 0x0000
	};
	size_t start = continuation - 0x08000000;
	uint32_t found = 0;
	for (size_t pos = start; pos < start + 0x80 && pos + 32 <= size; pos += 2) {
		bool matches = true;
		for (unsigned i = 0; i < sizeof(signature) / sizeof(*signature); ++i) {
			if (_read16(rom, pos + i * 2) != signature[i]) { matches = false; break; }
		}
		if (!matches || _read32(rom, pos + 28) != 0x68736D53) continue;
		if (found) return 0;
		found = pos + 0x08000000;
	}
	return found;
}

static bool _songClusterCalls(const uint8_t* rom, size_t size, const uint32_t starts[5],
                              uint32_t* start, uint32_t* stop, uint32_t* continuation) {
	*start = _thumbCallTarget(rom, size, starts[0], starts[1], 0, 0, 0);
	if (!*start || !_thumbCallTarget(rom, size, starts[1], starts[2], 0, 0, *start) ||
	    !_thumbCallTarget(rom, size, starts[2], starts[3], 0, 0, *start)) return false;
	*stop = _thumbCallTarget(rom, size, starts[3], starts[4], *start, 0, 0);
	*continuation = _thumbCallTarget(rom, size, starts[2], starts[3], *start, *stop, 0);
	return *stop && *continuation &&
		_thumbCallTarget(rom, size, starts[4], starts[4] + 0x40, *start, *stop, *continuation);
}

static bool _buildFunctionProfile(struct GBAMP2kProfile* profile,
	struct GBAMP2kFunction functions[9], const struct GBA* gba,
	uint32_t songTableOffset, unsigned songCount, uint32_t playerTableOffset,
	unsigned playerCount, uint32_t soundMode, bool ramVariant) {
	if (!profile || !functions || !gba || !gba->memory.rom ||
	    playerCount > GBA_MP2K_MAX_PLAYERS || !playerCount || !songCount ||
	    gba->memory.romSize < 0x200 ||
	    songTableOffset >= gba->memory.romSize || playerTableOffset >= gba->memory.romSize) return false;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	size_t size = gba->memory.romSize;
	uint32_t refs[5] = { 0 }, starts[5] = { 0 };
	unsigned found = 0;
	for (size_t pos = 4; pos + 4 <= size; pos += 4) {
		if (_read32(rom, pos - 4) != playerTableOffset + 0x08000000 ||
		    _read32(rom, pos) != songTableOffset + 0x08000000) continue;
		uint32_t entry = 0;
		for (size_t scan = pos - 4; scan >= (pos > 256 ? pos - 256 : 0) + 2; scan -= 2) {
			if ((_read16(rom, scan) & 0xFF00) == 0xB500) { entry = (uint32_t) scan; break; }
		}
		if (!entry) continue;
		if (found && (pos - refs[0] > 0x400 || entry == starts[found - 1])) found = 0;
		refs[found] = (uint32_t) pos;
		starts[found++] = entry;
		if (found == 5) {
			if (!ramVariant) break; /* Existing ROM-player selection stays unchanged. */
			uint32_t start, stop, continuation;
			bool checked = _songClusterCalls(rom, size, starts, &start, &stop, &continuation) &&
				_romPointer(gba, start, 2) && _read16(rom, start - 0x08000000) == 0xB5F0 &&
				_romPointer(gba, stop, 2) && _read16(rom, stop - 0x08000000) == 0xB570 &&
				_romPointer(gba, continuation, 2);
			for (unsigned i = 0; checked && i < 5; ++i)
				checked = _read16(rom, starts[i]) == 0xB500;
			if (checked) {
				uint16_t opcode = _read16(rom, continuation - 0x08000000);
				checked = opcode == 0x1C02 || opcode == 0xB500;
			}
			if (checked) break;
			/* Game wrappers can reference the same tables before the SDK.
			 * Reject that cluster, then continue the unchanged strict checks. */
			found = 0;
		}
	}
	if (found != 5) return false;
	uint32_t start, stop, continuation;
	if (!_songClusterCalls(rom, size, starts, &start, &stop, &continuation)) return false;
	const char* names[5] = { "m4aSongNumStart", "m4aSongNumStartOrChange",
		"m4aSongNumStartOrContinue", "m4aSongNumStop", "m4aSongNumContinue" };
	for (unsigned i = 0; i < 5; ++i) {
		functions[i] = (struct GBAMP2kFunction) {
			.address = starts[i] + 0x08000000, .parentReturnEnd = refs[i] + 0x08000000,
			.name = names[i], .type = i == 3 ? GBA_MP2K_SONG_STOP :
				i == 4 ? GBA_MP2K_MPLAY_CONTINUE : GBA_MP2K_SONG_START,
			.argument = GBA_MP2K_ARG_SONG, .firstOpcode = 0xB500
		};
	}
	functions[5] = (struct GBAMP2kFunction) {
		.address = start, .name = "m4aMPlayStart", .type = GBA_MP2K_MPLAY_START,
		.argument = GBA_MP2K_ARG_PLAYER, .firstOpcode = 0xB5F0
	};
	functions[6] = (struct GBAMP2kFunction) {
		.address = stop, .name = "m4aMPlayStop", .type = GBA_MP2K_MPLAY_STOP,
		.argument = GBA_MP2K_ARG_PLAYER, .firstOpcode = 0xB570
	};
	if (!_romPointer(gba, continuation, sizeof(uint16_t))) return false;
	uint16_t continueOpcode = _read16(rom, continuation - 0x08000000);
	if (continueOpcode != 0x1C02 && (!ramVariant || continueOpcode != 0xB500)) return false;
	functions[7] = (struct GBAMP2kFunction) {
		.address = continuation, .name = "m4aMPlayContinue", .type = GBA_MP2K_MPLAY_CONTINUE,
		.argument = GBA_MP2K_ARG_PLAYER, .firstOpcode = continueOpcode
	};
	unsigned functionCount = 8;
	uint32_t fade = _fadeNearContinue(rom, size, continuation);
	if (fade) {
		functions[functionCount++] = (struct GBAMP2kFunction) {
			.address = fade, .name = "m4aMPlayFadeOut", .type = GBA_MP2K_FADE_OUT,
			.argument = GBA_MP2K_ARG_PLAYER, .firstOpcode = 0x1C02
		};
	}
	for (unsigned i = 1; i < functionCount; ++i) {
		struct GBAMP2kFunction function = functions[i];
		unsigned j = i;
		while (j && functions[j - 1].address > function.address) {
			functions[j] = functions[j - 1];
			--j;
		}
		functions[j] = function;
	}
	memset(profile, 0, sizeof(*profile));
	profile->name = "runtime-mp2k";
	memcpy(profile->gameCode, rom + 0xAC, 4);
	profile->romCrc32 = gba->romCrc32;
	profile->romSize = size;
	profile->songTableOffset = songTableOffset;
	profile->songCount = songCount;
	profile->playerTableOffset = playerTableOffset;
	profile->playerCount = playerCount;
	profile->soundMode = soundMode;
	if (ramVariant)
		profile->playerBacking = GBA_MP2K_RAM_PLAYER;
	profile->functions = functions;
	profile->functionCount = functionCount;
	return GBAMP2kProfileValidate(profile, gba);
}

/* The scanner owns table/SoundMode detection. Adjacent initialization literals
 * add optional SoundInfo evidence; absence leaves ownership unverified. */
bool GBAMP2kProfileBuildRuntime(struct GBAMP2kProfile* profile,
	struct GBAMP2kFunction functions[9], const struct GBA* gba,
	uint32_t songTableOffset, unsigned songCount, uint32_t playerTableOffset,
	unsigned playerCount, uint32_t soundMode) {
	const char* experimental = getenv("MGBA_FIXED_AUDIO_EWRAM_EXPERIMENTAL_ALL");
	return GBAMP2kProfileBuildRuntimeWithPolicy(profile, functions, gba,
		songTableOffset, songCount, playerTableOffset, playerCount, soundMode,
		experimental && !strcmp(experimental, "1"));
}

bool GBAMP2kProfileBuildRuntimeWithPolicy(struct GBAMP2kProfile* profile,
	struct GBAMP2kFunction functions[9], const struct GBA* gba,
	uint32_t songTableOffset, unsigned songCount, uint32_t playerTableOffset,
	unsigned playerCount, uint32_t soundMode, bool experimental) {
	if (!profile || !functions || !gba || !gba->memory.rom || gba->memory.romSize < 0x200) return false;
	memset(profile, 0, sizeof(*profile));
	profile->name = "runtime-mp2k";
	memcpy(profile->gameCode, (const uint8_t*) gba->memory.rom + 0xAC, 4);
	for (unsigned i = 0; i < 4; ++i) if (profile->gameCode[i] < ' ' || profile->gameCode[i] > '~') profile->gameCode[i] = '?';
	profile->romCrc32 = gba->romCrc32;
	profile->romSize = gba->memory.romSize;
	profile->songTableOffset = songTableOffset;
	profile->songCount = songCount;
	profile->playerTableOffset = playerTableOffset;
	profile->playerCount = playerCount;
	profile->soundMode = soundMode;
	bool ramVariant = _runtimeRamBacking(gba, playerTableOffset, playerCount, experimental);
	if (ramVariant)
		profile->playerBacking = GBA_MP2K_RAM_PLAYER;
	profile->outputSampleRate = 32768;
	profile->confidence = 1;
	profile->polling = true;
	unsigned channels = (soundMode >> 8) & 15, frequency = (soundMode >> 16) & 15;
	unsigned dac = (soundMode >> 20) & 15;
	if (!channels || channels > 12 || !frequency || frequency > 12 || dac < 8 || dac > 11 ||
	    !GBAMP2kProfileValidate(profile, gba)) return false;
	struct GBAMP2kProfile entries;
	if (_buildFunctionProfile(&entries, functions, gba, songTableOffset, songCount,
	    playerTableOffset, playerCount, soundMode, ramVariant)) {
		profile->functions = functions;
		profile->functionCount = entries.functionCount;
		profile->polling = false;
		profile->confidence = 2;
		uint32_t first = functions[0].address, last = functions[entries.functionCount - 1].address;
		/* Approximate code range is diagnostic evidence, not an ownership grant. */
		profile->driverCodeStart = first > 0x08001000 ? first - 0x1000 : 0x08000000;
		profile->driverCodeEnd = last + 0x2000 < 0x08000000 + profile->romSize ?
			last + 0x2000 : 0x08000000 + profile->romSize;
	}
	/* RAM-backed output requires the complete, independently checked hook cluster. */
	if (profile->playerBacking == GBA_MP2K_RAM_PLAYER && profile->polling) return false;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	uint32_t soundAddress = 0;
	for (size_t pos = 20; pos + 16 <= gba->memory.romSize; pos += 4) {
		if (_read32(rom, pos) != soundMode || _read32(rom, pos + 4) != playerCount ||
		    (_read32(rom, pos + 8) != 0x08000000 + playerTableOffset &&
		     _read32(rom, pos + 12) != 0x08000000 + playerTableOffset)) continue;
		uint32_t address = _read32(rom, pos - 8);
		uint32_t cgb = _read32(rom, pos - 4);
		if (address < 0x03000000 || address > 0x03008000 - sizeof(struct GBAMP2kContext) ||
		    !_ramPointer(cgb)) continue;
		if (soundAddress && address != soundAddress) { profile->soundInfoPointerOffset = 0; break; }
		soundAddress = address;
		profile->soundInfoPointerOffset = pos - 8;
	}
	return GBAMP2kProfileValidate(profile, gba);
}
