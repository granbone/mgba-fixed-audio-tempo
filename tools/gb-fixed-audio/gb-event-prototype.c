/* GB1 actual native commands -> bounded queue -> independently clocked driver.
 * A separate full native-CPU continuation is the semantic reference. */
#define main bgmPrototypeMain
#include "gb-multi-driver-prototype.c"
#undef main
enum EventType { UNKNOWN_COMMAND,MUSIC_START,MUSIC_STOP,MUSIC_CHANGE,SE_START,MUSIC_PAUSE,MUSIC_RESUME,VOLUME_TRANSITION };
static const char* typeNames[]={"UNKNOWN_COMMAND","BGM_START","BGM_STOP","BGM_CHANGE","SE_START","MUSIC_PAUSE","MUSIC_RESUME","VOLUME_TRANSITION"};
struct Event { uint64_t sequence,audioTime; uint32_t gameTime; enum EventType type; unsigned entry,id; struct SM83RegisterFile regs; unsigned acceptedWrites; };
static enum EventType classify(const struct Event* e,const uint8_t* before,const uint8_t* after){
    if(BANK==15){if(e->entry==0x4009)return SE_START;if(e->id==0)return MUSIC_STOP;return (before[0]&0x7f)?MUSIC_CHANGE:MUSIC_START;}
    if(e->id==0xf0)return VOLUME_TRANSITION;
    if(e->id==0xfe&&!(before[2]&0x40)&&(after[2]&0x40))return MUSIC_PAUSE;
    if(e->id==0xff&&(before[2]&0x40)&&!(after[2]&0x40))return MUSIC_RESUME;
    if(after[4]==e->id&&(before[4]!=after[4]||memcmp(before+0x20,after+0x20,4)||memcmp(before+0x28,after+0x28,4)))return before[4]?MUSIC_CHANGE:MUSIC_START;
    if(memcmp(before+0x14,after+0x14,4)||memcmp(before+0x24,after+0x24,4)||memcmp(before+0x2c,after+0x2c,4))return SE_START;
    return UNKNOWN_COMMAND;
}
static struct Event queue[256];
static unsigned head,depth,maxDepth,overflow,outOfOrder,late,maxLateCycles,rejected,accepted,delivered,primaryMutation,ownershipViolation;
static uint64_t lastTime,nextSequence=1;
static uint32_t segmentGame;
static uint64_t segmentAudio;
static unsigned segmentSpeed;
static struct Context *gameContext,*oracleContext,*auxContext,*commandContext;
static unsigned commandWrites;
static uint64_t commandSequence;
static FILE *eventLog,*commandSemantic;
static void eventStore(struct SM83Core* cpu,uint16_t a,int8_t v){
    if(active==commandContext){
        if(a>=LO&&a<=HI&&!active->root)++commandWrites;
        if(a>=0xff10&&a<=0xff3f)fprintf(commandSemantic,"{\"context\":\"%s\",\"sequence\":%llu,\"address\":%u,\"value\":%u}\n",active==gameContext?"primary":active==oracleContext?"native":"aux",(unsigned long long)commandSequence,a,(uint8_t)v);
    }
    store(cpu,a,v);
}
static bool isCommand(struct Context* c){
    return !c->root&&!c->gb->cpu->irqPending&&c->gb->memory.currentBank==(int)BANK&&
        (BANK==5?c->gb->cpu->pc==0x4bec:c->gb->cpu->pc==0x4006||c->gb->cpu->pc==0x4009);
}
static void attachEvents(struct Context* c){attach(c);c->gb->cpu->memory.store8=eventStore;}
static void snapshotDriver(struct Context* c,uint8_t* data){for(unsigned a=LO;a<=HI;++a)data[a-LO]=view(c,a);}
static bool command(struct Context* c,const struct Event* event,bool inject){
    /* Full-game oracle can be inside OAM DMA at the mapped instant; native
     * game code also waits before its ROM far-call. Retain that bus guard. */
    if(inject&&c==oracleContext)while(c->gb->memory.dmaRemaining){step(c);drain(c);if(c->guard)return false;}
    struct SM83Core* cpu=c->gb->cpu;struct SM83RegisterFile saved=cpu->regs;unsigned savedBank=c->gb->memory.currentBank;
    bool savedAux=c->aux,savedRoot=c->root,savedIRQ=cpu->irqPending,savedHalt=cpu->halted;unsigned savedSteps=c->steps;uint32_t savedEntry=c->entryTime;
    bool savedIME=c->gb->memory.ime;uint8_t savedIE=c->gb->memory.ie;
    uint8_t scratch[256];memcpy(scratch,c->gb->memory.wram+SCRATCH-0xc000,256);
    if(inject){
        active=c;c->store(cpu,0x2000,BANK);cpu->regs=event->regs;cpu->pc=event->entry;cpu->sp=SCRATCH+254;
        cpu->executionState=SM83_CORE_FETCH;cpu->irqPending=false;cpu->halted=false;
        c->gb->memory.ie=0;c->gb->memory.ime=false;
        c->gb->memory.wram[SCRATCH-0xc000+254]=c->gb->memory.wram[SCRATCH-0xc000+255]=0;
        cpu->memory.setActiveRegion(cpu,cpu->pc);c->aux=true;
    }
    uint16_t sp=cpu->sp,ret=word(c,sp);c->root=false;c->entryTime=clockNow(c);c->steps=0;unsigned steps=0;
    commandContext=c;commandSequence=event->sequence;commandWrites=0;
    do{step(c);if(++steps>12000){++c->guard;break;}drain(c);}while(!c->guard&&!(cpu->sp==(uint16_t)(sp+2)&&cpu->pc==ret));
    commandContext=NULL;
    if(inject&&c==oracleContext){
        active=c;c->store(cpu,0x2000,savedBank);cpu->regs=saved;cpu->irqPending=savedIRQ;cpu->halted=savedHalt;cpu->executionState=SM83_CORE_FETCH;cpu->memory.setActiveRegion(cpu,cpu->pc);
        memcpy(c->gb->memory.wram+SCRATCH-0xc000,scratch,256);
        c->gb->memory.ie=savedIE;c->gb->memory.ime=savedIME;GBUpdateIRQs(c->gb);
    }
    c->aux=savedAux;c->root=savedRoot;c->steps=savedSteps;c->entryTime=savedEntry;return !c->guard;
}
static bool gameStep(struct Context* c){
    if(!isCommand(c)){step(c);return !c->guard;}
    struct Event e={0};e.sequence=nextSequence;e.entry=c->gb->cpu->pc;e.id=c->gb->cpu->a;e.regs=c->gb->cpu->regs;e.gameTime=clockNow(c);
    e.audioTime=segmentAudio+(uint32_t)(e.gameTime-segmentGame)/segmentSpeed;unsigned originalReturn=word(c,e.regs.sp);
    uint8_t before[256],after[256];snapshotDriver(c,before);
    if(!command(c,&e,false))return false;e.acceptedWrites=commandWrites;snapshotDriver(c,after);e.type=classify(&e,before,after);
    fprintf(eventLog,"{\"kind\":\"CLASSIFIED\",\"sequence\":%llu,\"type\":\"%s\",\"id\":%u}\n",(unsigned long long)e.sequence,typeNames[e.type],e.id);
    fprintf(eventLog,"{\"kind\":\"ACTUAL_COMMAND\",\"sequence\":%llu,\"gameTime\":%u,\"audioTime\":%llu,\"speed\":%u,\"entry\":%u,\"return\":%u,\"a\":%u,\"b\":%u,\"c\":%u,\"de\":%u,\"hl\":%u,\"acceptedWrites\":%u,\"before\":\"",(unsigned long long)e.sequence,e.gameTime,(unsigned long long)e.audioTime,segmentSpeed,e.entry,originalReturn,e.regs.a,e.regs.b,e.regs.c,e.regs.de,e.regs.hl,e.acceptedWrites);
    for(unsigned i=0;i<=HI-LO;++i)fprintf(eventLog,"%02x",before[i]);fputs("\",\"after\":\"",eventLog);for(unsigned i=0;i<=HI-LO;++i)fprintf(eventLog,"%02x",after[i]);fputs("\"}\n",eventLog);
    /* Native setter stores prove acceptance; a rejected priority request is
     * logged but never injected. No speculative allocation in this queue. */
    if(!e.acceptedWrites){++rejected;return true;}
    ++accepted;++nextSequence;if(e.audioTime<lastTime)++outOfOrder;lastTime=e.audioTime;
    if(depth==256){++overflow;return false;}queue[(head+depth)%256]=e;++depth;if(depth>maxDepth)maxDepth=depth;return true;
}
static void nativeUntil(struct Context* c,uint32_t deadline){while((int32_t)(deadline-clockNow(c))>0&&!c->guard){step(c);drain(c);}}
int main(int argc,char** argv){
    if(argc!=6){fprintf(stderr,"gb-event-prototype ROM 1x|2x|switch capture-frame ticks prefix\n");return 2;}
    profile=identifyProfile(argv[1]);
    unsigned capture=atoi(argv[3]),ticks=atoi(argv[4]);struct Context boot=create(argv[1]);attach(&boot);
    while(!(boot.gb->video.frameCounter>=capture&&boot.gb->cpu->pc==ROOT&&boot.gb->memory.currentBank==(int)BANK&&!boot.gb->cpu->irqPending)){step(&boot);drain(&boot);if(boot.gb->video.frameCounter>capture+10)return 6;}
    void* snapshot=malloc(boot.core->stateSize(boot.core));if(!snapshot||!boot.core->saveState(boot.core,snapshot))return 7;
    struct SM83RegisterFile regs=boot.gb->cpu->regs;
    struct Context game=create(argv[1]),audio=create(argv[1]),reference=create(argv[1]);struct Context* contexts[]={&game,&audio,&reference};
    for(unsigned i=0;i<3;++i){if(!contexts[i]->core->loadState(contexts[i]->core,snapshot))return 8;copyChannelContinuation(contexts[i],&boot);mAudioBufferClear(contexts[i]->core->getAudioBuffer(contexts[i]->core));attachEvents(contexts[i]);}
    free(snapshot);gameContext=&game;oracleContext=&reference;auxContext=&audio;audio.aux=true;audio.gb->memory.ie=0;audio.gb->memory.ime=false;audio.gb->cpu->irqPending=false;
    char path[2048];snprintf(path,sizeof(path),"%s-events.jsonl",argv[5]);eventLog=createFile(path);snprintf(path,sizeof(path),"%s-command-semantic.jsonl",argv[5]);commandSemantic=createFile(path);
    snprintf(path,sizeof(path),"%s-aux",argv[5]);files(&audio,path);snprintf(path,sizeof(path),"%s-native",argv[5]);files(&reference,path);
    uint32_t start=clockNow(&audio),gameStart=clockNow(&game);uint64_t elapsedGame=0;
    uint8_t initialRAM[8192];memcpy(initialRAM,audio.gb->memory.wram,8192);
    for(unsigned tick=0;tick<ticks&&!audio.guard&&!reference.guard&&!game.guard;++tick){
        segmentSpeed=!strcmp(argv[2],"2x")?2:!strcmp(argv[2],"switch")?(tick/50)%2+1:1;
        segmentGame=gameStart+(uint32_t)elapsedGame;segmentAudio=(uint64_t)tick*PERIOD;elapsedGame+=(uint64_t)segmentSpeed*PERIOD;
        while((int32_t)(gameStart+(uint32_t)elapsedGame-clockNow(&game))>0){unsigned f=game.gb->video.frameCounter,phase=f%180;game.core->setKeys(game.core,f>=180?(phase<3?8:phase>=40&&phase<43?1:phase>=80&&phase<83?128:phase>=120&&phase<123?16:0):0);if(!gameStep(&game))break;drain(&game);}
        /* CPU instruction/command completion may overshoot the speed slice.
         * Such commands retain their mapped future timestamp in the queue. */
        uint8_t primaryRAM[8192];memcpy(primaryRAM,game.gb->memory.wram,8192);
        idleUntil(&audio,start+tick*PERIOD);if(!invoke(&audio,&regs))break;
        while(reference.updates<=tick&&!reference.guard){step(&reference);drain(&reference);}
        uint64_t end=(uint64_t)(tick+1)*PERIOD;
        while(depth&&queue[head].audioTime<end){struct Event* e=&queue[head];
            uint32_t deadline=start+(uint32_t)e->audioTime;
            if((int32_t)(deadline-clockNow(&audio))<0){++late;unsigned delta=clockNow(&audio)-deadline;if(delta>maxLateCycles)maxLateCycles=delta;}else idleUntil(&audio,deadline);
            nativeUntil(&reference,deadline);
            if(!command(&audio,e,true)||!command(&reference,e,true))break;
            fprintf(eventLog,"{\"kind\":\"DELIVERED\",\"sequence\":%llu,\"audioTime\":%llu,\"auxClock\":%u,\"nativeClock\":%u}\n",(unsigned long long)e->sequence,(unsigned long long)e->audioTime,clockNow(&audio),clockNow(&reference));
            ++delivered;head=(head+1)%256;--depth;
        }
        idleUntil(&audio,start+(tick+1)*PERIOD);
        if(memcmp(primaryRAM,game.gb->memory.wram,8192)){++primaryMutation;break;}
        for(unsigned a=0;a<8192;++a){if((a>=LO-0xc000&&a<=HI-0xc000)||(a>=SCRATCH-0xc000&&a<SCRATCH-0xc000+256))continue;if(initialRAM[a]!=audio.gb->memory.wram[a]){++ownershipViolation;break;}}
        if(ownershipViolation)break;
    }
    drain(&audio);drain(&reference);fclose(eventLog);fclose(commandSemantic);
    snprintf(path,sizeof(path),"%s-summary.json",argv[5]);FILE* out=createFile(path);
    fprintf(out,"{\"mode\":\"%s\",\"ticks\":%u,\"auxUpdates\":%u,\"nativeUpdates\":%u,\"primaryUpdates\":%u,\"accepted\":%u,\"rejected\":%u,\"delivered\":%u,\"pending\":%u,\"overflow\":%u,\"outOfOrder\":%u,\"late\":%u,\"maxDepth\":%u,\"auxGuard\":%u,\"nativeGuard\":%u,\"primaryGuard\":%u,\"audioCycles\":%u,\"primaryCycles\":%u}\n",argv[2],ticks,audio.updates,reference.updates,game.updates,accepted,rejected,delivered,depth,overflow,outOfOrder,late,maxDepth,audio.guard,reference.guard,game.guard,clockNow(&audio)-start,clockNow(&game)-gameStart);fclose(out);
    snprintf(path,sizeof(path),"%s-checks.json",argv[5]);FILE* checks=createFile(path);fprintf(checks,"{\"maxLateCycles\":%u,\"maxLateMs\":%.6f,\"primaryRAMMutation\":%u,\"auxOwnershipViolation\":%u}\n",maxLateCycles,maxLateCycles/8388.608,primaryMutation,ownershipViolation);fclose(checks);
    bool failed=audio.guard||reference.guard||game.guard||overflow||outOfOrder||primaryMutation||ownershipViolation||audio.updates!=ticks||reference.updates!=ticks||delivered+depth!=accepted;
    closeContext(&boot);closeContext(&game);closeContext(&audio);closeContext(&reference);active=NULL;return failed?11:0;
}
