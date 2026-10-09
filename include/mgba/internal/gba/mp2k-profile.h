/* Copyright (c) 2026
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_MP2K_PROFILE_H
#define GBA_MP2K_PROFILE_H

#include <mgba/internal/gba/mp2k-events.h>

CXX_GUARD_START

enum GBAMP2kArgumentKind {
	GBA_MP2K_ARG_SONG,
	GBA_MP2K_ARG_PLAYER
};

struct GBAMP2kFunction {
	uint32_t address;
	uint32_t parentReturnEnd;
	const char* name;
	enum GBAMP2kEventType type;
	enum GBAMP2kArgumentKind argument;
	uint16_t firstOpcode;
};

/* Known profiles preserve validated behavior. Runtime profiles retain static
 * evidence; experimental output also requires live song/player validation. */
enum GBAMP2kPlayerBacking { GBA_MP2K_ROM_PLAYER, GBA_MP2K_RAM_PLAYER };

struct GBAMP2kProfile {
	const char* name;
	char gameCode[5];
	uint32_t romCrc32;
	size_t romSize;
	uint32_t songTableOffset;
	unsigned songCount;
	uint32_t playerTableOffset;
	unsigned playerCount;
	uint32_t soundMode;
	uint32_t soundInfoPointerOffset;
	uint32_t outputSampleRate;
	uint32_t lookaheadSamples;
	uint32_t driverCodeStart;
	uint32_t driverCodeEnd;
	uint32_t psgCodeStart;
	uint32_t psgCodeEnd;
	uint32_t firstTickCodeEnd;
	uint32_t fifoSource[4];
	const struct GBAMP2kFunction* functions;
	size_t functionCount;
	enum GBAMP2kPlayerBacking playerBacking;
	bool known;
	bool polling;
	unsigned confidence; /* 1: tables, 2: function candidates, 3: live player */
};

const struct GBAMP2kProfile* GBAMP2kKnownProfile(const struct GBA* gba);
bool GBAMP2kProfileValidate(const struct GBAMP2kProfile* profile, const struct GBA* gba);
bool GBAMP2kProfileBuildRuntime(struct GBAMP2kProfile* profile,
	struct GBAMP2kFunction functions[9], const struct GBA* gba,
	uint32_t songTableOffset, unsigned songCount, uint32_t playerTableOffset,
	unsigned playerCount, uint32_t soundMode);
/* Explicit policy is used by frontends; environment variables cannot override it. */
bool GBAMP2kProfileBuildRuntimeWithPolicy(struct GBAMP2kProfile* profile,
	struct GBAMP2kFunction functions[9], const struct GBA* gba,
	uint32_t songTableOffset, unsigned songCount, uint32_t playerTableOffset,
	unsigned playerCount, uint32_t soundMode, bool experimental);
bool GBAMP2kConservativeIdentity(const struct GBA* gba);
const struct GBAMP2kFunction* GBAMP2kProfileFunctionAt(const struct GBAMP2kProfile* profile,
	uint32_t address);

CXX_GUARD_END

#endif
