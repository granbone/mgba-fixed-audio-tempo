/* Native SDK cancellation and accepted/rejected Start evidence.
 * SPDX-License-Identifier: MPL-2.0. Controlled calls on private in-memory
 * fixtures, not gameplay or permission to suppress polling STOP.
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
#include <stdlib.h>
#include <string.h>
static unsigned stops, starts;
static struct GBAMP2kEvent lastStop;
static void sink(const struct GBAMP2kEvent* e, void* unused) {
 (void) unused;
 if (e->type==GBA_MP2K_MPLAY_STOP && e->playerGuardPassed) { ++stops; lastStop=*e; }
 if (e->type==GBA_MP2K_MPLAY_START && e->playerGuardPassed) ++starts;
}
static void call(struct mCore* c,uint32_t pc,uint32_t player,uint32_t arg) {
 struct ARMCore* cpu=((struct GBA*)c->board)->cpu;
 _ARMSetMode(cpu,MODE_THUMB);cpu->cpsr.t=MODE_THUMB;
 cpu->gprs[0]=player;cpu->gprs[1]=arg;cpu->gprs[ARM_LR]=0x08000001;
 cpu->gprs[ARM_PC]=pc;ThumbWritePC(cpu);
 unsigned n=0;while(cpu->gprs[ARM_PC]!=0x08000002 && n++<8192)c->step(c);
 assert(n<8192);
}
static void snapshot(const struct GBA* gba,const struct GBAMP2kProfile* profile,unsigned id,uint8_t* dst) {
 const struct GBAMP2kMusicPlayerInfo* p=GBAMP2kPlayerAt(gba,profile,id);assert(p&&p->trackCount<=16);
 const void* tracks=GBAMP2kPlayerRam(gba,p->tracks,p->trackCount*sizeof(struct GBAMP2kMusicPlayerTrack));assert(tracks);
 memset(dst,0,64+16*80);memcpy(dst,p,64);memcpy(dst+64,tracks,p->trackCount*80);
}
int main(int argc,char** argv) {
 if(argc!=14)return 2;
 uint32_t st=strtoul(argv[3],NULL,0),pt=strtoul(argv[5],NULL,0),mode=strtoul(argv[7],NULL,0);
 unsigned sc=atoi(argv[4]),pc=atoi(argv[6]),id=atoi(argv[8]),song=atoi(argv[9]);
 uint32_t startPC=strtoul(argv[10],NULL,0),stopPC=strtoul(argv[11],NULL,0),alias=strtoul(argv[12],NULL,0);
 uint32_t mainPC=strtoul(argv[13],NULL,0);
 struct mCore* c=GBACoreCreate();assert(c&&c->init(c));mCoreInitConfig(c,NULL);mCoreLoadConfig(c);
 struct VFile* f=VFileOpen(argv[1],O_RDONLY);assert(f&&c->loadROM(c,f));c->reset(c);
 f=VFileOpen(argv[2],O_RDONLY);assert(f&&mCoreLoadStateNamed(c,f,0));f->close(f);
 struct GBA* gba=c->board;struct GBAMP2kProfile profile;struct GBAMP2kFunction functions[9];
 /* Isolated ABI calls stand in for an IRQ-masked native driver invocation.
  * Mask hardware IRQ only in this test machine, not any product guard. A
  * pending game interrupt cannot safely enter a synthetic return address. */
 c->busWrite16(c,0x04000208,0);
 assert(GBAMP2kProfileBuildRuntimeWithPolicy(&profile,functions,gba,st,sc,pt,pc,mode,true));
 assert(GBAMP2kEventsUseRuntimeProfile(c,&profile));GBAMP2kEventsSetSink(c,sink,NULL);
 struct mAudioClock clock;mAudioClockInit(&clock,59.727501,32768);mAudioClockSetFrontendRate(&clock,true,119.455002);
 GBAMP2kEventsSetAudioClock(c,&clock);struct GBAMP2kFrontendState front={0};
 front.throttleStateKnown=true;front.throttleMode=GBA_MP2K_THROTTLE_FAST_FORWARD;front.runRate=119.455002f;
 GBAMP2kEventsSetFrontendState(c,&front);
 uint32_t address=c->busRead32(c,0x08000000+pt+id*12),header=c->busRead32(c,0x08000000+st+song*8);
 const struct GBAMP2kMusicPlayerInfo* p=GBAMP2kPlayerAt(gba,&profile,id);assert(p);
 call(c,startPC,address,header);assert(p->magic==MP2K_MAGIC&&p->songHeader==header&&p->clock==0&&p->status==0);
 /* Step native sequencer via its registered MPlayMain: after calling Start
  * the original CPU continuation is no longer a game loop. Its callback is
  * obtained from the live initialized player chain, not a global ROM match. */
 bool referenced=false;
 for(unsigned i=0;i<profile.playerCount;++i){const struct GBAMP2kMusicPlayerInfo* q=GBAMP2kPlayerAt(gba,&profile,i);if(q&&q->func==(mainPC|1U))referenced=true;}
 assert(referenced);
 unsigned ticks=0;
 /* Main's ABI accepts a player in r0. IRQ wrappers/other SoundInfo functions
  * are not substituted. This is a bounded controlled SDK call. */
 while(!(p->status&0x80000000U) && ticks++<2048)call(c,mainPC,address,0);
 assert(ticks<2048&&p->clock>0);
 uint32_t fineClock=p->clock;
 uint8_t before[64+16*80],after[64+16*80];snapshot(gba,&profile,id,before);
 unsigned n=stops;call(c,stopPC,address,0);snapshot(gba,&profile,id,after);
 bool identical=!memcmp(before,after,sizeof(before));
 printf("[PHASE8 NATIVE] game=%s kind=EXPLICIT_STOP_AFTER_FINE player=%u song=%u clock=%u status=%08x same_ram=%d sink_delta=%u song_in_sink=%d\n",profile.gameCode,id,song,p->clock,p->status,identical,stops-n,lastStop.songId);
 assert(identical&&stops==n+1&&lastStop.playerId==(int)id);
 unsigned aliasDelta=0;
 if(alias){assert(!GBAMP2kProfileFunctionAt(&profile,alias));n=stops;call(c,alias,address,0);snapshot(gba,&profile,id,after);aliasDelta=stops-n;
  assert(!memcmp(before,after,sizeof(before))&&aliasDelta==0);
  printf("[PHASE8 NATIVE] game=%s kind=ALTERNATE_SDK_STOP_AFTER_FINE pc=%08x same_ram=1 sink_delta=0 classification=UNKNOWN_OUTSIDE_HOOK_PROFILE\n",profile.gameCode,alias);
 }
 call(c,startPC,address,header);assert(p->clock==0&&p->status==0&&p->songHeader==header);
 bool aliasBeforeFine=false;
 if(alias){
  n=stops;call(c,alias,address,0);
  assert(p->status==0x80000000U&&stops==n);
  aliasBeforeFine=true;
  printf("[PHASE8 NATIVE] game=%s kind=ALTERNATE_SDK_STOP_BEFORE_FINE pc=%08x native_status=%08x sink_delta=0\n",profile.gameCode,alias,p->status);
  call(c,startPC,address,header);assert(p->clock==0&&p->status==0);
 }
 call(c,mainPC,address,0);assert(p->clock>0);uint32_t previousClock=p->clock;
 call(c,startPC,address,header);assert(p->clock==0&&p->status==0&&p->songHeader==header);
 /* Native rejection is deliberately induced only in this private RAM. */
 bool priorityCheck=strcmp(profile.gameCode,"A8CJ")!=0;
 if(priorityCheck){
  c->busWrite8(c,address+9,255);c->busWrite8(c,address+11,1);
  c->busWrite8(c,p->tracks,c->busRead8(c,p->tracks)|0x40);
 }else c->busWrite32(c,address+0x34,MP2K_MAGIC+2); /* this SDK has no priority branch */
 snapshot(gba,&profile,id,before);n=starts;
 call(c,startPC,address,header);snapshot(gba,&profile,id,after);
 bool rejectedUnchanged=!memcmp(before,after,sizeof(before));assert(rejectedUnchanged);
 unsigned rejectedSink=starts-n;
 if(!priorityCheck)c->busWrite32(c,address+0x34,MP2K_MAGIC);
 n=stops;call(c,stopPC,address,0);assert(p->status==0x80000000U&&stops==n+1);
 printf("{\"game\":\"%s\",\"controlled_native_calls\":true,\"fine_clock\":%u,\"stop_after_fine_same_ram\":%s,\"canonical_stop_observed\":true,\"alternate_stop_pc\":%u,\"alternate_stop_sink_delta\":%u,\"alternate_stop_before_fine_unobserved\":%s,\"same_header_reuse_resets_clock\":true,\"previous_clock\":%u,\"rejected_start_ram_unchanged\":true,\"rejection_kind\":\"%s\",\"rejected_start_guarded_sink_emissions\":%u,\"explicit_stop_before_fine_observed\":true,\"production_policy_changed\":false}\n",profile.gameCode,fineClock,identical?"true":"false",alias,aliasDelta,aliasBeforeFine?"true":"false",previousClock,priorityCheck?"PRIORITY":"MAGIC_GUARD",rejectedSink);
 mCoreConfigDeinit(&c->config);c->deinit(c);return 0;
}
