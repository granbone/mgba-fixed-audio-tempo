/* One exact-ROM, DMG, standalone research prototype. Native mGBA APU only.
 * No NRxx replay, no primary RAM writes, no production libretro route. */
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
#include <io.h>
#include <sys/stat.h>

#define PERIOD 139264U /* Actual native TMA=188, TAC=4, DMG timing units. */
#define ROOT 0x4dfd
#define COMMAND 0x4bec
struct Context {
    struct mCore* core;struct GB* gb;
    void (*store)(struct SM83Core*,uint16_t,int8_t);
    uint8_t (*load)(struct SM83Core*,uint16_t);
    unsigned pc,updates,commands,guard,steps,maxSteps,maxCycles,unexpectedReads;
    unsigned primaryMutation,ownershipViolation;
    bool root,aux;uint16_t sp,ret;uint32_t entryTime;
    FILE *semantic,*pcm,*state,*roots;
    uint64_t samples;
};
static struct Context* active;
static FILE* createFile(const char* path){
    int fd=_open(path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
    return fd<0?NULL:_fdopen(fd,"wb");
}
static uint32_t clockNow(struct Context* c){return (uint32_t)mTimingCurrentTime(&c->gb->timing);}
static uint8_t view(struct Context* c,uint16_t a){return GBView8(c->gb->cpu,a,-1);}
static uint16_t word(struct Context* c,uint16_t a){return view(c,a)|(view(c,a+1)<<8);}
static void store(struct SM83Core* cpu,uint16_t address,int8_t signedValue){
    struct Context* c=active;uint8_t value=signedValue;
    bool audioIO=(address>=0xff10 && address<=0xff14)||(address>=0xff16 && address<=0xff1e)||(address>=0xff20 && address<=0xff26)||(address>=0xff30 && address<=0xff3f);
    if(c->aux && !((address>=0xce27 && address<=0xcf26)||(address>=0xdf00 && address<=0xdfff)||audioIO)){
        ++c->guard;fprintf(stderr,"unsafe write pc=%04x address=%04x value=%02x\n",c->pc,address,value);return;
    }
    if(c->root && address>=0xff10 && address<=0xff3f && c->semantic)
        fprintf(c->semantic,"{\"tick\":%u,\"cycle\":%u,\"pc\":%u,\"address\":%u,\"value\":%u}\n",c->updates,clockNow(c),c->pc,address,value);
    c->store(cpu,address,signedValue);
}
static uint8_t load(struct SM83Core* cpu,uint16_t address){
    struct Context* c=active;
    if(c->aux && !(address<0x8000 || (address>=0xce27 && address<=0xcf26) || (address>=0xdf00 && address<=0xdfff))){
        ++c->guard;++c->unexpectedReads;fprintf(stderr,"unsafe read pc=%04x address=%04x\n",c->pc,address);return 0;
    }
    return c->load(cpu,address);
}
static void attach(struct Context* c){c->store=c->gb->cpu->memory.store8;c->gb->cpu->memory.store8=store;c->load=c->gb->cpu->memory.load8;c->gb->cpu->memory.load8=load;}
static void copyChannelContinuation(struct Context* dst,const struct Context* src){
    /* The existing disk format omits transient wave window/sample and noise
     * coalescing fields. A memory clone can retain all four channel PODs from
     * the loaded primary, without copying pointers, queues, timing events or
     * frontend PCM. No savestate-format or production-core change. */
    dst->gb->audio.ch1=src->gb->audio.ch1;dst->gb->audio.ch2=src->gb->audio.ch2;
    dst->gb->audio.ch3=src->gb->audio.ch3;dst->gb->audio.ch4=src->gb->audio.ch4;
}
static struct Context create(const char* rom){
    struct Context c={0};c.core=GBCoreCreate();if(!c.core||!c.core->init(c.core))exit(3);
    mCoreInitConfig(c.core,NULL);mCoreConfigSetValue(&c.core->config,"gb.model","DMG");mCoreConfigSetValue(&c.core->config,"sgb.model","DMG");
    c.core->opts.frameskip=9;c.core->opts.volume=0x100;c.core->loadConfig(c.core,&c.core->config);
    struct VFile* vf=VFileOpen(rom,O_RDONLY);if(!vf||!c.core->loadROM(c.core,vf))exit(4);
    c.core->reset(c.core);c.core->setAudioBufferSize(c.core,16384);c.gb=c.core->board;
    if(c.gb->romCrc32!=0x690227f6 || c.gb->memory.romSize!=262144 || c.gb->model!=GB_MODEL_DMG){fprintf(stderr,"exact research identity mismatch\n");exit(12);}
    return c;
}
static void drain(struct Context* c){
    int16_t b[32768];size_t n=mAudioBufferRead(c->core->getAudioBuffer(c->core),b,16384);c->samples+=n;
    if(c->pcm)fwrite(b,4,n,c->pcm);
}
static void beginRoot(struct Context* c){
    struct SM83Core* cpu=c->gb->cpu;c->root=true;c->sp=cpu->sp;c->ret=word(c,cpu->sp);c->entryTime=clockNow(c);c->steps=0;
    if(c->roots)fprintf(c->roots,"{\"kind\":\"ENTRY\",\"tick\":%u,\"cycle\":%u,\"sp\":%u,\"return\":%u}\n",c->updates,c->entryTime,cpu->sp,c->ret);
}
static void endRoot(struct Context* c){
    if(c->steps>c->maxSteps)c->maxSteps=c->steps;unsigned cycles=clockNow(c)-c->entryTime;if(cycles>c->maxCycles)c->maxCycles=cycles;
    /* Raw IO shadow and wave storage: diagnostic reads must not run the APU. */
    if(c->state){for(unsigned a=0xce27;a<=0xcf26;++a)fputc(view(c,a),c->state);fwrite(c->gb->memory.io+0x10,1,32,c->state);fwrite(c->gb->audio.ch3.wavedata8,1,16,c->state);}
    if(c->roots)fprintf(c->roots,"{\"kind\":\"RETURN\",\"tick\":%u,\"cycle\":%u,\"duration\":%u,\"steps\":%u}\n",c->updates,clockNow(c),cycles,c->steps);
    c->root=false;++c->updates;drain(c);
}
static void step(struct Context* c){
    active=c;struct SM83Core* cpu=c->gb->cpu;c->pc=cpu->pc;
    if(!c->root && cpu->pc==ROOT && c->gb->memory.currentBank==5 && !cpu->irqPending)beginRoot(c);
    if(cpu->pc==COMMAND && c->gb->memory.currentBank==5 && !cpu->irqPending){
        ++c->commands;if(c->roots)fprintf(c->roots,"{\"kind\":\"COMMAND\",\"cycle\":%u,\"a\":%u,\"c\":%u,\"withinUpdate\":%d}\n",clockNow(c),cpu->a,cpu->c,c->root);
    }
    if(c->aux && (c->gb->memory.currentBank!=5 || cpu->pc<0x4bec || cpu->pc>=0x5900 || ++c->steps>6000 || clockNow(c)-c->entryTime>100000)){
        ++c->guard;fprintf(stderr,"unsafe execution pc=%04x bank=%d steps=%u cycles=%u\n",cpu->pc,c->gb->memory.currentBank,c->steps,clockNow(c)-c->entryTime);return;
    }
    if(!c->aux && c->root)++c->steps;
    c->core->step(c->core);
    if(c->root && cpu->pc==c->ret && cpu->sp==(uint16_t)(c->sp+2))endRoot(c);
}
static void idleUntil(struct Context* c,uint32_t deadline){
    /* Run existing hardware event scheduler, with CPU dispatch suspended.
     * Keep DIV/timer, video deadlines, and native APU events at 1x. */
    active=c;struct SM83Core* cpu=c->gb->cpu;
    while((int32_t)(deadline-clockNow(c))>0){
        int32_t remaining=deadline-clockNow(c),until=cpu->nextEvent-cpu->cycles;
        if(until<=0){cpu->irqh.processEvents(cpu);continue;}
        cpu->cycles+=remaining<until?remaining:until;
        if(cpu->cycles>=cpu->nextEvent)cpu->irqh.processEvents(cpu);
        drain(c);
    }
}
static bool invoke(struct Context* c,const struct SM83RegisterFile* regs){
    struct SM83Core* cpu=c->gb->cpu;cpu->regs=*regs;
    cpu->pc=ROOT;cpu->sp=0xdffe;cpu->executionState=SM83_CORE_FETCH;cpu->irqPending=false;cpu->halted=false;
    /* Dedicated aux scratch stack. Sentinel is never executed. */
    c->gb->memory.wram[0x1ffe]=0;c->gb->memory.wram[0x1fff]=0;cpu->memory.setActiveRegion(cpu,ROOT);
    beginRoot(c);do{step(c);}while(c->root && !c->guard);return !c->guard;
}
static void files(struct Context* c,const char* prefix){
    char path[2048];snprintf(path,sizeof(path),"%s-semantic.jsonl",prefix);c->semantic=createFile(path);
    snprintf(path,sizeof(path),"%s.s16le",prefix);c->pcm=createFile(path);
    snprintf(path,sizeof(path),"%s-driver-state.bin",prefix);c->state=createFile(path);
    snprintf(path,sizeof(path),"%s-roots.jsonl",prefix);c->roots=createFile(path);
    if(!c->semantic||!c->pcm||!c->state||!c->roots)exit(5);
}
static void closeContext(struct Context* c){
    if(c->pcm)fclose(c->pcm);if(c->state)fclose(c->state);if(c->semantic)fclose(c->semantic);if(c->roots)fclose(c->roots);
    mCoreConfigDeinit(&c->core->config);c->core->deinit(c->core);
}
int main(int argc,char** argv){
    if(argc!=6){fprintf(stderr,"gb-bgm-prototype ROM native|aux|game2x|switch capture-frame ticks output-prefix\n");return 2;}
    bool guardCase=!strncmp(argv[2],"guard-",6),original=!strcmp(argv[2],"native-original"),native=!strcmp(argv[2],"native")||original,game2x=!strcmp(argv[2],"game2x"),sw=!strcmp(argv[2],"switch");
    if(!guardCase && !native && !game2x && !sw && strcmp(argv[2],"aux"))return 2;
    unsigned capture=strtoul(argv[3],NULL,10),ticks=strtoul(argv[4],NULL,10);
    struct Context bootstrap=create(argv[1]);attach(&bootstrap);
    /* Snapshot a real playing native state immediately before update entry. */
    while(!(bootstrap.gb->video.frameCounter>=capture && bootstrap.gb->cpu->pc==ROOT && bootstrap.gb->memory.currentBank==5 && !bootstrap.gb->cpu->irqPending)){
        step(&bootstrap);if(bootstrap.gb->video.frameCounter>capture+10)return 6;drain(&bootstrap);
    }
    void* snapshot=malloc(bootstrap.core->stateSize(bootstrap.core));if(!snapshot||!bootstrap.core->saveState(bootstrap.core,snapshot))return 7;
    struct SM83RegisterFile regs=bootstrap.gb->cpu->regs;
    struct Context audio=original?bootstrap:create(argv[1]);if(!original && !audio.core->loadState(audio.core,snapshot))return 8;
    if(!original)copyChannelContinuation(&audio,&bootstrap);
    mAudioBufferClear(audio.core->getAudioBuffer(audio.core));if(!original)attach(&audio);audio.aux=!native;
    audio.updates=audio.commands=audio.steps=audio.maxSteps=audio.maxCycles=0;audio.samples=0;audio.root=false;
    struct Context primary={0};if(!native){primary=create(argv[1]);if(!primary.core->loadState(primary.core,snapshot))return 9;copyChannelContinuation(&primary,&bootstrap);attach(&primary);}
    free(snapshot);uint32_t start=clockNow(&audio),primaryStart=native?0:clockNow(&primary);unsigned primaryFrame=native?0:primary.gb->video.frameCounter;
    files(&audio,argv[5]);
    if(!native){audio.gb->memory.ie=0;audio.gb->memory.ime=false;audio.gb->cpu->irqPending=false;}
    uint8_t initialRAM[8192];memcpy(initialRAM,audio.gb->memory.wram,8192);
    if(guardCase){
        active=&audio;
        if(!strcmp(argv[2],"guard-write"))store(audio.gb->cpu,0xc000,0x77);
        else if(!strcmp(argv[2],"guard-io"))store(audio.gb->cpu,0xff04,0x77);
        else if(!strcmp(argv[2],"guard-read"))load(audio.gb->cpu,0xc000);
        else if(!strcmp(argv[2],"guard-pc")){audio.gb->cpu->pc=0x100;step(&audio);}
        else if(!strcmp(argv[2],"guard-budget")){beginRoot(&audio);audio.steps=6000;step(&audio);}
        else return 2;
        if(memcmp(initialRAM,audio.gb->memory.wram,8192))++audio.ownershipViolation;
    }
    else if(native){while(audio.updates<ticks && !audio.guard)step(&audio);drain(&audio);}
    else{
        unsigned elapsed=0;
        for(unsigned tick=0;tick<ticks && !audio.guard;++tick){
            unsigned speed=game2x?2:sw?(tick/100)%2+1:1;
            elapsed+=speed*PERIOD;
            while((int32_t)(primaryStart+elapsed-clockNow(&primary))>0){step(&primary);if(primary.gb->video.frameCounter%4==0)drain(&primary);}
            idleUntil(&audio,start+tick*PERIOD);
            uint8_t primaryRAM[8192];memcpy(primaryRAM,primary.gb->memory.wram,8192);
            if(!invoke(&audio,&regs))break;
            if(memcmp(primaryRAM,primary.gb->memory.wram,8192)){++audio.primaryMutation;++audio.guard;break;}
            for(unsigned a=0;a<8192;++a){
                if((a>=0xe27 && a<=0xf26)||(a>=0x1f00 && a<=0x1fff))continue;
                if(audio.gb->memory.wram[a]!=initialRAM[a]){++audio.ownershipViolation;++audio.guard;break;}
            }
            idleUntil(&audio,start+(tick+1)*PERIOD);
        }
    }
    char path[2048];snprintf(path,sizeof(path),"%s-summary.json",argv[5]);FILE* out=createFile(path);if(!out)return 10;
    fprintf(out,"{\"mode\":\"%s\",\"captureFrame\":%u,\"requestedTicks\":%u,\"updates\":%u,\"guard\":%u,\"maxInstructions\":%u,\"maxCycles\":%u,\"samples\":%llu,\"sampleRate\":%u,\"audioCycles\":%u,\"primaryCycles\":%u,\"primaryFrames\":%u,\"primaryUpdates\":%u,\"primaryCommands\":%u,\"auxCommands\":%u,\"eventTransfer\":\"initial-state-only\",\"stateSize\":%llu,\"model\":%u,\"doubleSpeed\":%u}\n",argv[2],capture,ticks,audio.updates,audio.guard,audio.maxSteps,audio.maxCycles,(unsigned long long)audio.samples,audio.core->audioSampleRate(audio.core),clockNow(&audio)-start,native?0:clockNow(&primary)-primaryStart,native?0:primary.gb->video.frameCounter-primaryFrame,native?0:primary.updates,native?0:primary.commands,audio.commands,(unsigned long long)audio.core->stateSize(audio.core),audio.gb->model,audio.gb->doubleSpeed);
    fclose(out);bool failed=guardCase?(audio.guard!=1 || audio.ownershipViolation!=0):(audio.guard || audio.updates!=ticks || audio.primaryMutation || audio.ownershipViolation);
    snprintf(path,sizeof(path),"%s-checks.json",argv[5]);out=createFile(path);if(!out)return 10;
    fprintf(out,"{\"primaryRAMMutation\":%u,\"auxOwnershipViolation\":%u,\"unexpectedReads\":%u,\"guardExpected\":%s,\"passed\":%s}\n",audio.primaryMutation,audio.ownershipViolation,audio.unexpectedReads,guardCase?"true":"false",failed?"false":"true");fclose(out);
    if(!original)closeContext(&bootstrap);closeContext(&audio);if(!native)closeContext(&primary);return failed?11:0;
}
