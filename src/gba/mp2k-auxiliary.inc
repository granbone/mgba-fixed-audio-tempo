/* SPDX-License-Identifier: MPL-2.0
 * Non-activating recognition of the SDK's two idle auxiliary MusicPlayers.
 * Addresses/capacities come from checked native initialization instructions,
 * not from a guessed layout of the profile's normal player slots. */
static uint32_t _auxThumbCall(const struct GBA* gba, uint32_t pc) {
	if (!_romPointer(gba, pc, 4)) return 0;
	const uint8_t* bytes = (const void*) gba->memory.rom;
	uint16_t high = _read16(bytes, pc - 0x08000000), low = _read16(bytes, pc - 0x08000000 + 2);
	if ((high & 0xF800) != 0xF000 || (low & 0xF800) != 0xF800) return 0;
	int32_t displacement = (high & 0x7FF) * 4096;
	if (displacement & 0x400000) displacement -= 0x800000;
	return pc + 4 + displacement + (low & 0x7FF) * 2;
}

static uint32_t _auxThumbLiteral(const struct GBA* gba, uint32_t pc) {
	if (!_romPointer(gba, pc, 2)) return 0;
	const uint8_t* rom = (const void*) gba->memory.rom;
	uint16_t opcode = _read16(rom, pc - 0x08000000);
	if ((opcode & 0xF800) != 0x4800) return 0;
	uint32_t at = ((pc + 4) & ~3U) + (opcode & 0xFF) * 4;
	return _romPointer(gba, at, 4) ? _read32(rom, at - 0x08000000) : 0;
}

static bool _auxCheckedOpen(const struct GBA* gba, uint32_t pc, uint32_t* main) {
	static const uint16_t code[] = {
		0xB5F0,0x1C07,0x1C0E,0x0612,0x0E14,0x2C00,0xD02A,0x2C10,0xD900,0x2410,
		0x4815,0x6805,0x6829,0x4815,0x4281,0xD121,0x1C48,0x6028,0x1C38,0,0,
		0x62FE,0x723C,0x2080,0x0600,0x6078,0x2C00,0xD007,0x2100,0x7031,
		0x1E60,0x0600,0x0E04,0x3650,0x2C00,0xD1F8,0x6A28,0x2800,0xD004,
		0x63B8,0x6A68,0x63F8,0x2000,0x6228,0x626F,0x4806,0x6228,0x4804,
		0x6028,0x6378,0xBCF0,0xBC01,0x4700,0
	};
	if (!_romPointer(gba, pc, 120)) return false;
	const uint8_t* bytes = (const uint8_t*) gba->memory.rom + pc - 0x08000000;
	for (unsigned i = 0; i < sizeof(code) / sizeof(*code); ++i)
		if (i != 19 && i != 20 && _read16(bytes, i * 2) != code[i]) return false;
	uint32_t clear = _auxThumbCall(gba, pc + 38);
	*main = _read32(bytes, 116);
	return _romPointer(gba, clear, 2) && _read32(bytes, 112) == MP2K_MAGIC &&
		(*main & 1) && _romPointer(gba, *main & ~1U, 2);
}

static bool _auxCheckedInit(const struct GBAMP2kEvents* events, uint32_t base, uint32_t* tracks, uint32_t* main) {
	static const uint16_t code[] = { 0x2500,0x2600,0x01A8,0,0x1840,0,0x1934,
		0x1C21,0x2202,0,0,0x2000,0x6220,0x36A0,0x3501,0x2D01,0xDDF0,0xBC70,0xBC01,0x4700 };
	uint32_t first = UINT32_MAX;
	for (size_t i = 0; i < events->profile->functionCount; ++i) {
		const struct GBAMP2kFunction* f = &events->profile->functions[i];
		if (f->argument == GBA_MP2K_ARG_SONG && f->type == GBA_MP2K_SONG_START && f->address < first) first = f->address;
	}
	if (first == UINT32_MAX) return false;
	const uint8_t* rom = (const void*) events->gba->memory.rom;
	unsigned matches = 0;
	uint32_t begin = first >= 0x08000200 ? first - 0x200 : 0x08000000;
	for (uint32_t pc = begin; pc < first; pc += 2) {
		if (!_romPointer(events->gba, pc, sizeof(code))) continue;
		const uint8_t* b = rom + pc - 0x08000000;
		bool valid = true;
		for (unsigned i = 0; i < sizeof(code) / sizeof(*code); ++i) {
			if (i == 3 || i == 5 || i == 9 || i == 10) continue;
			if (_read16(b, i * 2) != code[i]) { valid = false; break; }
		}
		if (!valid || (_read16(b, 6) & 0xFF00) != 0x4900 || (_read16(b, 10) & 0xFF00) != 0x4C00 ||
		    _auxThumbLiteral(events->gba, pc + 6) != base) continue;
		uint32_t nativeMain = 0;
		if (!_auxCheckedOpen(events->gba, _auxThumbCall(events->gba, pc + 18), &nativeMain)) continue;
		*tracks = _auxThumbLiteral(events->gba, pc + 10); *main = nativeMain; ++matches;
	}
	return matches == 1;
}

static bool _auxOverlap(uint32_t a, size_t an, uint32_t b, size_t bn) {
	return (uint64_t) a < (uint64_t) b + bn && (uint64_t) b < (uint64_t) a + an;
}

static bool _auxiliaryIdleStop(const struct GBAMP2kEvents* events, uint32_t stop, uint32_t ret,
                              uint32_t arg, uint32_t r5, uint32_t* tracks) {
	static const uint16_t wrapper[] = {0xB530,0x480D,0x0400,0x0C00,0x2800,0xD008,0x4D0B,0x1C04,
		0x6828,0,0,0x350C,0x3C01,0x2C00,0xD1F8,0x4D08,0x2401,0x1C28,0,0,
		0x3540,0x3C01,0x2C00,0xDAF8,0xBC30,0xBC01,0x4700,0};
	static const uint16_t stopCode[] = {0xB570,0x1C06,0x6B71,0x480D,0x4281,0xD114,0x1C48,0x6370,
		0x6870,0x2180,0x0609,0x4308,0x6070,0x7A34,0x6AF5,0x2C00,0xDD07,0x1C30,
		0x1C29,0,0,0x3C01,0x3550,0x2C00,0xDCF7,0x4802,0x6370,0xBC70,0xBC01,0x4700};
	if (events->profile->playerBacking != GBA_MP2K_RAM_PLAYER || !GBAMP2kProfileValidate(events->profile, events->gba) || arg != r5 || ret < 0x08000028 ||
	    !_romPointer(events->gba, ret - 40, 68) || !_romPointer(events->gba, stop, 64)) return false;
	const uint8_t* rom = (const void*) events->gba->memory.rom;
	uint32_t caller = ret - 40; const uint8_t* b = rom + caller - 0x08000000;
	for (unsigned i = 0; i < sizeof(wrapper) / sizeof(*wrapper); ++i)
		if (i != 9 && i != 10 && i != 18 && i != 19 && _read16(b, i * 2) != wrapper[i]) return false;
	if (_auxThumbCall(events->gba, caller + 18) != stop || _auxThumbCall(events->gba, caller + 36) != stop ||
	    _read32(b, 56) != events->profile->playerCount || _read32(b, 60) != 0x08000000 + events->profile->playerTableOffset) return false;
	uint32_t base = _read32(b, 64), trackBase = 0, main = 0;
	if (base < 0x02000000 || !GBAMP2kPlayerRam(events->gba, base, 128) ||
	    (arg != base && arg != base + 64) || !_auxCheckedInit(events, base, &trackBase, &main) ||
	    !GBAMP2kPlayerRam(events->gba, trackBase, 320) || _auxOverlap(base,128,trackBase,320)) return false;
	for (unsigned i = 0; i < events->profile->playerCount; ++i) {
		uint32_t at = events->profile->playerTableOffset + i * 12;
		uint32_t player = _read32(rom, at), track = _read32(rom, at + 4);
		size_t bytes = _read16(rom, at + 8) * 80;
		if ((player && (_auxOverlap(base,128,player,64) || _auxOverlap(trackBase,320,player,64))) ||
		    (bytes && (_auxOverlap(base,128,track,bytes) || _auxOverlap(trackBase,320,track,bytes)))) return false;
	}
	b = rom + stop - 0x08000000;
	for (unsigned i = 0; i < sizeof(stopCode) / sizeof(*stopCode); ++i)
		if (i != 19 && i != 20 && _read16(b, i * 2) != stopCode[i]) return false;
	if (_read32(b, 60) != MP2K_MAGIC) return false;
	uint32_t trackStop = _auxThumbCall(events->gba, stop + 38);
	if (!_romPointer(events->gba, trackStop, 62)) return false;
	b = rom + trackStop - 0x08000000;
	static const uint16_t trackPrefix[] = {0xB570,0x1C0D,0x7829,0x2080,0x4208,0xD015};
	for (unsigned i = 0; i < sizeof(trackPrefix) / sizeof(*trackPrefix); ++i)
		if (_read16(b, i * 2) != trackPrefix[i]) return false;
	if (_read16(b,56) != 0xBC70 || _read16(b,58) != 0xBC01 || _read16(b,60) != 0x4700) return false;
	const struct GBAMP2kMusicPlayerInfo* n = GBAMP2kPlayerRam(events->gba, arg, 64);
	*tracks = trackBase + (arg == base ? 0 : 160);
	uint8_t expected[64] = {0};
	uint32_t paused = 0x80000000, magic = MP2K_MAGIC;
	memcpy(expected+4,&paused,4); expected[8]=2; memcpy(expected+44,tracks,4);
	memcpy(expected+52,&magic,4); memcpy(expected+56,&main,4);
	bool linkKnown = n->intp == base && arg != base;
	for (unsigned i = 0; i < events->profile->playerCount; ++i)
		if (n->intp && n->intp == _read32(rom,events->profile->playerTableOffset+i*12)) linkKnown = true;
	if (!linkKnown) return false;
	memcpy(expected+60,&n->intp,4);
	if (memcmp(n,expected,64)) return false;
	const uint8_t* data = GBAMP2kPlayerRam(events->gba, *tracks, 160);
	for (unsigned i = 0; i < 160; ++i) if (data[i]) return false;
	return GBAMP2kProfileValidate(events->profile, events->gba);
}

static uint32_t _invalidPlayerRamHash(const struct GBAMP2kEvents* events, const char* phase) {
	uint32_t hash = _eventRamDiagnostic(events, phase);
	if (events->invalidPlayerAuxiliaryStop) {
		const uint8_t* p = GBAMP2kPlayerRam(events->gba, events->invalidPlayerAuxiliaryAddress, 64);
		const uint8_t* t = GBAMP2kPlayerRam(events->gba, events->invalidPlayerAuxiliaryTracks, 160);
		if (!p || !t) return hash ^ UINT32_MAX;
		for (unsigned i = 0; i < 64; ++i) hash = (hash ^ p[i]) * UINT32_C(16777619);
		for (unsigned i = 0; i < 160; ++i) hash = (hash ^ t[i]) * UINT32_C(16777619);
	}
	return hash;
}
