/* GB1 research-only dynamic CALL/IRQ ancestry and sound-root ranking evidence. */
#include <mgba/gb/core.h>
#include <mgba/core/core.h>
#include <mgba/core/timing.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
struct Routine { unsigned pc,bank,calls,returns,apu,wave,freq,volume,frames,lastFrame,irqMask,maxApu,ramCount,pcMin,pcMax; uint64_t targets; uint32_t first,last; unsigned char ram[8192],reads[8192],code[8192]; };
struct Invocation { unsigned id,ret,sp,apu,irq; uint32_t start; };
static struct Routine routines[4096]; static struct Invocation stack[128];
static unsigned count,depth,frame,pc,op,irqVector,irqCounts[5],overflow;
static struct GB* gb; static struct mCore* core;
static FILE* entries;
static void (*nativeStore)(struct SM83Core*,uint16_t,int8_t);
static uint8_t (*nativeLoad)(struct SM83Core*,uint16_t);
static uint16_t (*nativeIRQ)(struct SM83Core*);
static uint32_t now(void){return (uint32_t)mTimingCurrentTime(&gb->timing);}
static unsigned bank(unsigned a){return a<0x4000?gb->memory.currentBank0:a<0x8000?gb->memory.currentBank:65535;}
static unsigned view(unsigned a){return GBView8(gb->cpu,a,-1);}
static bool ram(unsigned a){return (a>=0xa000&&a<0xe000)||(a>=0xff80&&a<0xffff);}
static bool stackOp(void){return op==0xc5||op==0xd5||op==0xe5||op==0xf5||op==0xc1||op==0xd1||op==0xe1||op==0xf1||op==0xcd||(op&0xe7)==0xc4||(op&0xc7)==0xc7||op==0xc9||op==0xd9||(op&0xe7)==0xc0||irqVector;}
static void bit(unsigned char* b,unsigned a){b[a>>3]|=1<<(a&7);}
static void store(struct SM83Core* c,uint16_t a,int8_t v){
 for(int j=(int)depth-1;j>=0;--j){struct Invocation* s=&stack[j];struct Routine* r=&routines[s->id];
  if(a>=0xff10&&a<=0xff3f){++r->apu;++s->apu;r->targets|=UINT64_C(1)<<(a-0xff10);if(a>=0xff30)++r->wave;if(a==0xff13||a==0xff14||a==0xff18||a==0xff19||a==0xff1d||a==0xff1e||a==0xff22)++r->freq;if(a==0xff12||a==0xff17||a==0xff21||a==0xff1c)++r->volume;if(r->lastFrame!=frame){r->lastFrame=frame;++r->frames;}}
  if(ram(a)&&!stackOp())bit(r->ram,a);
  if(s->irq)break;
 }nativeStore(c,a,v);
}
static uint8_t load(struct SM83Core* c,uint16_t a){
 if(ram(a)&&!stackOp())for(int j=(int)depth-1;j>=0;--j){bit(routines[stack[j].id].reads,a);if(stack[j].irq)break;}
 return nativeLoad(c,a);
}
static uint16_t irq(struct SM83Core* c){irqVector=nativeIRQ(c);if(irqVector>=0x40&&irqVector<=0x60)++irqCounts[(irqVector-0x40)/8];return irqVector;}
static void push(unsigned entry,unsigned ret,unsigned sp,unsigned isIRQ){
 unsigned b=bank(entry),id=0;for(;id<count;++id)if(routines[id].pc==entry&&routines[id].bank==b)break;
 if(id==count){if(count==4096){++overflow;return;}++count;routines[id].pc=entry;routines[id].bank=b;routines[id].pcMin=65535;routines[id].first=now();routines[id].lastFrame=UINT_MAX;}
 struct Routine* r=&routines[id];++r->calls;r->last=now();
 unsigned mask=0;for(unsigned j=0;j<depth;++j)mask|=stack[j].irq; r->irqMask|=mask|isIRQ;
 if(depth==128){++overflow;return;}stack[depth++]=(struct Invocation){id,ret,sp,0,isIRQ,now()};
}
static void ranges(FILE* f,unsigned char* map){bool first=true;fputc('[',f);for(unsigned a=0;a<65536;){if(!(map[a>>3]&(1<<(a&7)))){++a;continue;}unsigned lo=a++;while(a<65536&&(map[a>>3]&(1<<(a&7))))++a;fprintf(f,"%s[%u,%u]",first?"":",",lo,a-1);first=false;}fputc(']',f);}
int main(int argc,char** argv){
 if(argc<4){fprintf(stderr,"gb-runtime-discover ROM frames prefix [input]\n");return 2;}
 core=GBCoreCreate();if(!core||!core->init(core))return 3;mCoreInitConfig(core,NULL);
 const char* keys[]={"gb.model","sgb.model","cgb.hybridModel","cgb.sgbModel"};for(unsigned j=0;j<4;++j)mCoreConfigSetValue(&core->config,keys[j],"DMG");
 core->opts.volume=256;core->opts.frameskip=0;core->loadConfig(core,&core->config);
 mColor* pixels=calloc(160*144,sizeof(mColor));core->setVideoBuffer(core,pixels,160);
 struct VFile* rom=VFileOpen(argv[1],O_RDONLY);if(!rom||!core->loadROM(core,rom))return 4;core->reset(core);core->setAudioBufferSize(core,8192);gb=core->board;
 nativeStore=gb->cpu->memory.store8;nativeLoad=gb->cpu->memory.load8;nativeIRQ=gb->cpu->irqh.irqVector;gb->cpu->memory.store8=store;gb->cpu->memory.load8=load;gb->cpu->irqh.irqVector=irq;
 char entryPath[2048];snprintf(entryPath,sizeof(entryPath),"%s-entries.jsonl",argv[3]);entries=fopen(entryPath,"wb");if(!entries)return 5;
 unsigned limit=atoi(argv[2]);int16_t pcm[16384];uint32_t beginClock=now();
 /* LCD-off titles still receive a bounded hardware-clock fixture. */
 while((uint32_t)(now()-beginClock)<limit*140448U){struct SM83Core* c=gb->cpu;frame=gb->video.frameCounter;pc=c->pc;op=view(pc);unsigned preSP=c->sp;irqVector=0;
  if(argc>4&&atoi(argv[4])){unsigned phase=frame%180;core->setKeys(core,frame>=180?(phase<3?8:phase>=40&&phase<43?1:phase>=80&&phase<83?128:phase>=120&&phase<123?16:0):0);}
  if(!c->irqPending&&((bank(pc)==15&&(pc==0x4003||pc==0x4000||pc==0x4006||pc==0x4009||pc==0x400c||pc==0x4012||pc==0x4021||pc==0x402d||pc==0x404e))||(bank(pc)==5&&(pc==0x4bec||pc==0x4dfd))))fprintf(entries,"{\"frame\":%u,\"clock\":%u,\"entry\":%u,\"bank\":%u,\"return\":%u,\"sp\":%u,\"af\":%u,\"a\":%u,\"b\":%u,\"c\":%u,\"bc\":%u,\"de\":%u,\"hl\":%u,\"tma\":%u,\"tac\":%u,\"driver\":\"%02x%02x%02x%02x\"}\n",frame,now(),pc,bank(pc),view(c->sp)|(view(c->sp+1)<<8),c->sp,c->af,c->a,c->b,c->c,c->bc,c->de,c->hl,gb->memory.io[6],gb->memory.io[7],view(0xde80),view(0xde81),view(0xde82),view(0xde83));
  for(int j=(int)depth-1;j>=0;--j){struct Routine* r=&routines[stack[j].id];if(bank(pc)==r->bank){bit(r->code,pc);if(pc<r->pcMin)r->pcMin=pc;if(pc>r->pcMax)r->pcMax=pc;}if(stack[j].irq)break;}
  /* A STOP/LCD-off title need not reach FETCH again without input. Bound the
   * microstep loop by the same hardware clock, instead of waiting forever. */
  do{SM83Tick(c);}while(c->executionState!=SM83_CORE_FETCH&&(uint32_t)(now()-beginClock)<limit*140448U);
  if(c->executionState!=SM83_CORE_FETCH)break;
  if(irqVector){push(c->pc,pc,c->sp,1<<((irqVector-0x40)/8));}
  else if(c->sp==(uint16_t)(preSP-2)&&(op==0xcd||(op&0xe7)==0xc4||(op&0xc7)==0xc7)){unsigned ret=view(c->sp)|(view(c->sp+1)<<8);push(c->pc,ret,c->sp,0);}
  /* Actual PUSH-return; JP(HL) far calls (not arbitrary bank tail jumps).
   * The return on the native stack must be this instruction's next PC. */
  else if(op==0xe9&&c->pc>=0x4000&&c->pc<0x8000&&(view(c->sp)|(view(c->sp+1)<<8))==pc+1)push(c->pc,pc+1,c->sp,0);
  /* Bank-call RST helpers adjust the saved return past inline arguments.
   * A return must unwind by the actual stack boundary, not its old PC. */
  while(depth&&c->sp>stack[depth-1].sp){struct Invocation* s=&stack[--depth];struct Routine* r=&routines[s->id];++r->returns;if(s->apu>r->maxApu)r->maxApu=s->apu;}
  if(gb->video.frameCounter!=frame)mAudioBufferRead(core->getAudioBuffer(core),pcm,8192);
 }
 char path[2048];snprintf(path,sizeof(path),"%s.json",argv[3]);FILE* out=fopen(path,"wb");if(!out)return 5;
 fprintf(out,"{\"frames\":%u,\"overflow\":%u,\"openCallDepth\":%u,\"routines\":[",limit,overflow,depth);
 bool first=true;for(unsigned j=0;j<count;++j){struct Routine* r=&routines[j];if(!r->apu)continue;fprintf(out,"%s{\"entry\":%u,\"bank\":%u,\"calls\":%u,\"returns\":%u,\"apuWrites\":%u,\"waveWrites\":%u,\"frequencyWrites\":%u,\"volumeWrites\":%u,\"audioFrames\":%u,\"maxWritesPerCall\":%u,\"irqMask\":%u,\"firstClock\":%u,\"lastClock\":%u,\"targets\":\"%012llx\",\"pcRanges\":",first?"":",",r->pc,r->bank,r->calls,r->returns,r->apu,r->wave,r->freq,r->volume,r->frames,r->maxApu,r->irqMask,r->first,r->last,(unsigned long long)r->targets);first=false;ranges(out,r->code);fputs(",\"ramWrites\":",out);ranges(out,r->ram);fputs(",\"ramReads\":",out);ranges(out,r->reads);fputc('}',out);}
 fputs("]}\n",out);fclose(out);fclose(entries);snprintf(path,sizeof(path),"%s-screen.raw",argv[3]);out=fopen(path,"wb");fwrite(pixels,sizeof(mColor),160*144,out);fclose(out);free(pixels);mCoreConfigDeinit(&core->config);core->deinit(core);return overflow?6:0;
}
