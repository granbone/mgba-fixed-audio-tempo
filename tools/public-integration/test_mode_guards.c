/* SPDX-License-Identifier: MPL-2.0. Synthetic data only; no game bytes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/mp2k-profile.h>
#include "../../src/platform/libretro/fixed_audio_mode.h"
static void word(uint8_t* bytes, unsigned offset, uint32_t value) {
	for (unsigned i=0;i<4;++i) bytes[offset+i]=(uint8_t)(value>>(8*i));
}
int main(void) {
	assert(fixedAudioResolveMode(NULL,NULL,NULL,NULL)==FIXED_AUDIO_EXPERIMENTAL);
	assert(fixedAudioResolveMode("disabled",NULL,"1","1")==FIXED_AUDIO_DISABLED);
	assert(fixedAudioResolveMode("experimental","OFF",NULL,"1")==FIXED_AUDIO_DISABLED);
	assert(fixedAudioResolveMode("experimental",NULL,"0","1")==FIXED_AUDIO_DISABLED);
	assert(fixedAudioResolveMode("conservative",NULL,"1","1")==FIXED_AUDIO_CONSERVATIVE);
	assert(fixedAudioResolveMode(NULL,NULL,"1",NULL)==FIXED_AUDIO_CONSERVATIVE);
	assert(fixedAudioResolveMode(NULL,NULL,"1","1")==FIXED_AUDIO_EXPERIMENTAL);
	struct GBA* gba=calloc(1,sizeof(*gba)); uint8_t rom[1024]={0};
	gba->memory.rom=(uint32_t*)rom; gba->memory.romSize=sizeof(rom);
	struct GBAMP2kProfile p={.songTableOffset=0x200,.songCount=1,.playerTableOffset=0x220,
		.playerCount=1,.playerBacking=GBA_MP2K_RAM_PLAYER};
	word(rom,0x220,0x02000000); word(rom,0x224,0x02000100); word(rom,0x228,1);
	assert(GBAMP2kProfileValidate(&p,gba));
	word(rom,0x220,0x0203FFF0);assert(!GBAMP2kProfileValidate(&p,gba));
	word(rom,0x220,0x02000000);word(rom,0x224,0x0203FFF0);assert(!GBAMP2kProfileValidate(&p,gba));
	word(rom,0x224,0x01000000);assert(!GBAMP2kProfileValidate(&p,gba));
	word(rom,0x224,0x02000000);assert(!GBAMP2kProfileValidate(&p,gba));
	word(rom,0x224,0x02000100);word(rom,0x228,17);assert(!GBAMP2kProfileValidate(&p,gba));
	word(rom,0x228,1);word(rom,0x200,0x080003FC);assert(!GBAMP2kProfileValidate(&p,gba));
	word(rom,0x200,0);p.songCount=65536;assert(!GBAMP2kProfileValidate(&p,gba));
	p.songCount=1;p.playerTableOffset=1020;assert(!GBAMP2kProfileValidate(&p,gba));
	free(gba);puts("MODE PRECEDENCE AND ROM/RAM/OVERLAP/PLAYER/TRACK BOUNDS PASS");
	return 0;
}
