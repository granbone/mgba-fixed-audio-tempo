/* Native-function lifetime tests on a private in-memory AORJ fixture.
 * SPDX-License-Identifier: MPL-2.0. Inputs opened read-only; no guard bypass.
 * Explicit Stop is an injected native function call, not a gameplay recording.
 */
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/audio-clock.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/mp2k-events.h>
#include <mgba/internal/gba/mp2k-profile.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba-util/vfs.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <math.h>
static unsigned stops;
static struct GBAMP2kEvent last;
static void sink(const struct GBAMP2kEvent* e, void* unused) {
 (void)unused; if(e->type==GBA_MP2K_MPLAY_STOP){++stops;last=*e;}
}
static void nativeCall(struct mCore* c, uint32_t address, uint32_t arg1) {
 struct GBA* gba=c->board;struct ARMCore* cpu=gba->cpu;
 _ARMSetMode(cpu,MODE_THUMB);cpu->cpsr.t=MODE_THUMB;
 cpu->gprs[0]=0x03006d10;cpu->gprs[1]=arg1;cpu->gprs[ARM_LR]=0x08000001;
 cpu->gprs[ARM_PC]=address;ThumbWritePC(cpu);
 unsigned steps=0;
 while(cpu->gprs[ARM_PC]!=0x08000002 && steps++<4096)c->step(c);
 assert(steps<4096); /* bounded actual native body, never execute ROM header */
}
static void speed(struct mCore* c, struct mAudioClock* clock, float value, bool known) {
 struct GBAMP2kFrontendState f={0}; f.throttleStateKnown=known;
 f.throttleMode=value>1?GBA_MP2K_THROTTLE_FAST_FORWARD:GBA_MP2K_THROTTLE_NORMAL;
 f.runRate=59.727501f*value; f.fastForwardKnown=known; f.fastForwardActive=value>1;
 GBAMP2kEventsSetFrontendState(c,&f); mAudioClockSetFrontendRate(clock,known,f.runRate);
}
int main(int argc,char** argv){
 if(argc!=4)return 2;
 unsigned requested=argv[3][0]-'0';assert(requested==2||requested==3);
 struct mCore* c=GBACoreCreate(); assert(c&&c->init(c));mCoreInitConfig(c,NULL);mCoreLoadConfig(c);
 struct VFile* rom=VFileOpen(argv[1],O_RDONLY);assert(rom&&c->loadROM(c,rom));c->reset(c);
 struct VFile* state=VFileOpen(argv[2],O_RDONLY);assert(state&&mCoreLoadStateNamed(c,state,0));state->close(state);
 struct GBA* gba=c->board;struct GBAMP2kProfile profile;struct GBAMP2kFunction functions[9];
 assert(GBAMP2kProfileBuildRuntimeWithPolicy(&profile,functions,gba,838128,965,838080,4,9951232,true));
 assert(profile.polling&&profile.romCrc32==0x05e80ecc);
 assert(GBAMP2kEventsUseRuntimeProfile(c,&profile));GBAMP2kEventsSetSink(c,sink,NULL);
 struct mAudioClock clock;mAudioClockInit(&clock,59.727501,32768);GBAMP2kEventsSetAudioClock(c,&clock);speed(c,&clock,3,true);
 assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 for(unsigned f=0;f<37;++f){c->setKeys(c,f==19?1:0);c->runFrame(c);}
 const struct GBAMP2kMusicPlayerInfo* p=GBAMP2kPlayerAt(gba,&profile,2);
 assert(p&&p->status==0x80000000u&&p->clock==16&&stops==0);
 assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,0));
 speed(c,&clock,1,true);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 speed(c,&clock,2.5,true);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 speed(c,&clock,3,false);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 speed(c,&clock,0,true);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 speed(c,&clock,3,true);
 clock.frontendRunRate=NAN;assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));speed(c,&clock,3,true);
 uint32_t old=c->busRead32(c,0x03006d3c);c->busWrite32(c,0x03006d3c,0xfffffff0);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));c->busWrite32(c,0x03006d3c,old);
 c->busWrite8(c,0x03005a10,0x80);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));c->busWrite8(c,0x03005a10,0);
 uint32_t header=c->busRead32(c,0x03006d10);c->busWrite32(c,0x03006d10,header+4);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));c->busWrite32(c,0x03006d10,header);
 uint32_t ticks=c->busRead32(c,0x03006d1c);c->busWrite32(c,0x03006d1c,17);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));c->busWrite32(c,0x03006d1c,ticks);
 c->busWrite32(c,0x03005a30,0x03006000);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));c->busWrite32(c,0x03005a30,0);
 speed(c,&clock,requested,true);
 GBAMP2kEventsSuspend(c);assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 /* Rebinding discards proof; reach the actual finish instruction again on
  * this private fixture, rather than exposing a policy override. */
 assert(GBAMP2kEventsRebind(c,NULL));GBAMP2kEventsSetSink(c,sink,NULL);
 state=VFileOpen(argv[2],O_RDONLY);assert(state&&mCoreLoadStateNamed(c,state,0));state->close(state);
 speed(c,&clock,requested,true);
 for(unsigned f=0;f<37;++f){c->setKeys(c,f==19?1:0);c->runFrame(c);}
 assert(GBAMP2kEventsTakeAorjNaturalFinish(c,2));assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 /* A Start entry has not yet accepted/replaced the native owner. It must
  * not erase cancellation tracking on the still-owned finished SE. */
 _ARMSetMode(gba->cpu,MODE_THUMB);gba->cpu->cpsr.t=MODE_THUMB;
 gba->cpu->gprs[0]=0x03006d10;gba->cpu->gprs[ARM_PC]=0x08082074;
 ThumbWritePC(gba->cpu);gba->cpu->gprs[ARM_PC]+=2;c->step(c);
 /* A real Stop entry after natural completion, and after switching to 1x,
  * must reach the normal semantic sink exactly once with correct owner. */
 speed(c,&clock,1,true);uint32_t returnPC=gba->cpu->gprs[ARM_PC]-4;
 gba->cpu->gprs[0]=0x03006d10;gba->cpu->gprs[ARM_LR]=returnPC|1;
 _ARMSetMode(gba->cpu,MODE_THUMB);gba->cpu->cpsr.t=MODE_THUMB;gba->cpu->gprs[ARM_PC]=0x08082158;ThumbWritePC(gba->cpu);gba->cpu->gprs[ARM_PC]+=2;
 c->step(c);assert(stops==1&&last.playerId==2&&last.songId==202&&last.playerGuardPassed&&last.audioSampleTimestampKnown);
 assert(GBAMP2kEventsRebind(c,NULL));assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 gba->cpu->gprs[0]=0x03006d10;gba->cpu->gprs[ARM_PC]=0x08082158;ThumbWritePC(gba->cpu);gba->cpu->gprs[ARM_PC]+=2;c->step(c);assert(stops==1);
 /* Execute accepted same-header reuse, then an explicit Stop before FINE.
  * Stop leaves track enabled, unlike the verified natural completion. */
 nativeCall(c,0x08082074,0x0816bd40);
 p=GBAMP2kPlayerAt(gba,&profile,2);assert(p&&p->magic==MP2K_MAGIC&&p->clock==0&&p->status==0);
 uint8_t flags=c->busRead8(c,0x03005a10);
 assert((flags&0x80)&&!GBAMP2kEventsTakeAorjNaturalFinish(c,2));
 nativeCall(c,0x08082158,0);
 assert(p->status==0x80000000u&&c->busRead8(c,0x03005a10)==flags);
 assert(!GBAMP2kEventsTakeAorjNaturalFinish(c,2)&&stops==1);
 printf("{\"passed\":true,\"speed\":%u,\"cases\":21,\"native_finish_clock\":16,\"explicit_stop_owner\":2,\"explicit_stop_song\":202,\"explicit_stop_after_speed_change\":true,\"start_entry_retains_cancellation\":true,\"accepted_same_header_reuse\":true,\"explicit_stop_before_fine_not_suppressed\":true,\"rebind_discards_lifetime\":true}\n",requested);
 mCoreConfigDeinit(&c->config);c->deinit(c);return 0;
}
