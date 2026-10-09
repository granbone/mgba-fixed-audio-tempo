/* SPDX-License-Identifier: MPL-2.0
 * Read-only static eligibility using the production scanner and profile builder.
 * This does not execute a game or certify live playback. */
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/mp2k-profile.h>
#include "../mp2k-audio-trace/bridge/bridge.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
	if (argc != 4) return 2;
	FILE* file = fopen(argv[2], "rb");
	if (!file) return 2;
	fseek(file, 0, SEEK_END); long size = ftell(file); rewind(file);
	if (size < 0x200 || size > 0x2000000) { fclose(file); return 2; }
	uint8_t* rom = malloc(size);
	struct GBA* gba = calloc(1, sizeof(*gba));
	if (!rom || !gba || fread(rom, 1, size, file) != (size_t) size) return 2;
	fclose(file);
	gba->memory.rom = (uint32_t*) rom; gba->memory.romSize = size;
	gba->romCrc32 = strtoul(argv[3], NULL, 16);
	HMODULE library = LoadLibraryA(argv[1]); if (!library) return 2;
	int (*scan)(const uint8_t*, size_t, struct mp2k_bridge_scan_result*, size_t, size_t*) =
		(void*) GetProcAddress(library, "mp2k_bridge_scan");
	if (!scan) return 2;
	struct mp2k_bridge_scan_result result[16] = {0}; size_t count = 0;
	int scanned = scan(rom, size, result, 16, &count) == 0;
	struct GBAMP2kProfile profile; struct GBAMP2kFunction functions[9];
	int valid = scanned && count == 1 && result[0].player_table_reference_valid &&
		GBAMP2kProfileBuildRuntimeWithPolicy(&profile, functions, gba,
			result[0].song_table_offset, result[0].song_count, result[0].player_table_offset,
			result[0].player_count, result[0].sound_mode, true);
	if (GBAMP2kKnownProfile(gba)) valid = 1;
	printf("{\"static_eligible\":%s,\"scanner_tables\":%zu,\"reason\":\"%s\"}\n",
		valid ? "true" : "false", count, valid ? "PROFILE_VALID_LIVE_CHECKS_PENDING" :
		!scanned ? "SCANNER_FAILED" : !count ? "NO_MP2K_TABLE" : count != 1 ? "AMBIGUOUS_TABLES" :
		!result[0].player_table_reference_valid ? "INVALID_TABLE_REFERENCE" : "PROFILE_TABLE_HOOK_OR_SOUNDMODE_REJECTED");
	FreeLibrary(library); free(gba); free(rom); return 0;
}
