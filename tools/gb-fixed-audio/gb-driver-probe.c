/* GB0 selected-driver discovery: actual instruction PC and native call boundary.
 * This is a standalone probe, never a libretro fixed-audio route. */
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

static struct mCore* core;
static struct GB* gb;
static void (*originalStore)(struct SM83Core*,uint16_t,int8_t);
static uint8_t (*originalLoad)(struct SM83Core*,uint16_t);
static unsigned instructionPC, frame, calls, commandCalls, rootWrites, rootReads;
static bool inRoot;
static uint16_t rootSP, returnPC;
static uint32_t rootTime;
static unsigned reads[65536],writes[65536],pcs[65536],targets[65536];
static unsigned driverWrites[65536],gameWrites[65536],gameReads[65536];
static FILE* callLog;
static uint8_t view(uint16_t a){return GBView8(gb->cpu,a,-1);}
static uint16_t word(uint16_t a){return view(a)|view(a+1)<<8;}
static uint32_t clockNow(void){return (uint32_t)mTimingCurrentTime(&gb->timing);}
static bool driverPC(void){return gb->memory.currentBank==5 && instructionPC>=0x4bec && instructionPC<0x5900;}
static void store(struct SM83Core* c,uint16_t a,int8_t v){
    if(driverPC())++driverWrites[a];
    else if(frame>=100 && a>=0xce27 && a<=0xcf26)++gameWrites[a];
    if(inRoot && driverPC()){
        ++writes[a];++rootWrites;
        if(a>=0xff10 && a<=0xff3f)++targets[a];
    }
    originalStore(c,a,v);
}
static uint8_t load(struct SM83Core* c,uint16_t a){
    if(!driverPC() && frame>=100 && a>=0xce27 && a<=0xcf26)++gameReads[a];
    if(inRoot && driverPC()){++reads[a];++rootReads;}
    return originalLoad(c,a);
}
static struct mCore* create(const char* romPath){
    struct mCore* c=GBCoreCreate();if(!c||!c->init(c))return NULL;
    mCoreInitConfig(c,NULL);mCoreConfigSetValue(&c->config,"gb.model","DMG");mCoreConfigSetValue(&c->config,"sgb.model","DMG");
    c->opts.frameskip=9;c->opts.volume=0x100;c->loadConfig(c,&c->config);
    struct VFile* rom=VFileOpen(romPath,O_RDONLY);if(!rom||!c->loadROM(c,rom))return NULL;
    c->reset(c);c->setAudioBufferSize(c,8192);return c;
}
static void ranges(FILE* f,unsigned* map,unsigned lo,unsigned hi){
    bool first=true;fputc('[',f);
    for(unsigned a=lo;a<hi;){if(!map[a]){++a;continue;}unsigned start=a,count=0;
        while(a<hi && map[a])count+=map[a++];
        fprintf(f,"%s{\"start\":%u,\"end\":%u,\"accesses\":%u}",first?"":",",start,a-1,count);first=false;
    }fputc(']',f);
}
int main(int argc,char** argv){
    if(argc<4 || argc>5){fprintf(stderr,"gb-driver-probe ROM frames output-prefix [input=0|1]\n");return 2;}
    unsigned limit=strtoul(argv[2],NULL,10);core=create(argv[1]);if(!core)return 3;gb=core->board;
    originalStore=gb->cpu->memory.store8;originalLoad=gb->cpu->memory.load8;
    gb->cpu->memory.store8=store;gb->cpu->memory.load8=load;
    char path[2048];snprintf(path,sizeof(path),"%s-calls.jsonl",argv[3]);callLog=fopen(path,"wb");if(!callLog)return 4;
    int16_t pcm[16384];
    while(gb->video.frameCounter<limit || inRoot){
        struct SM83Core* c=gb->cpu;frame=gb->video.frameCounter;instructionPC=c->pc;
        if(argc==5 && atoi(argv[4]))core->setKeys(core,frame>=180 && frame<183?8:frame>=240?((frame%60<3?1:0)|16):0);
        if(c->pc==0x4dfd && gb->memory.currentBank==5 && !c->irqPending && !inRoot){
            inRoot=true;rootSP=c->sp;returnPC=word(c->sp);rootTime=clockNow();rootWrites=rootReads=0;++calls;
            fprintf(callLog,"{\"kind\":\"UPDATE_ENTRY\",\"frame\":%u,\"cycle\":%u,\"entry\":%u,\"return\":%u,\"sp\":%u,\"af\":%u,\"bc\":%u,\"de\":%u,\"hl\":%u}\n",frame,rootTime,c->pc,returnPC,c->sp,c->af,c->bc,c->de,c->hl);
        }
        if(c->pc==0x4bec && gb->memory.currentBank==5 && !c->irqPending){
            ++commandCalls;fprintf(callLog,"{\"kind\":\"COMMAND_ENTRY\",\"frame\":%u,\"cycle\":%u,\"a\":%u,\"c\":%u,\"sp\":%u,\"return\":%u,\"withinUpdate\":%d}\n",frame,clockNow(),c->a,c->c,c->sp,word(c->sp),inRoot);
        }
        if(inRoot && driverPC())++pcs[instructionPC];
        core->step(core);
        if(inRoot && c->sp==rootSP+2 && c->pc==returnPC){
            fprintf(callLog,"{\"kind\":\"UPDATE_RETURN\",\"frame\":%u,\"cycle\":%u,\"duration\":%u,\"pc\":%u,\"reads\":%u,\"writes\":%u}\n",frame,clockNow(),clockNow()-rootTime,c->pc,rootReads,rootWrites);inRoot=false;
        }
        if(gb->video.frameCounter!=frame)mAudioBufferRead(core->getAudioBuffer(core),pcm,8192);
    }
    fclose(callLog);snprintf(path,sizeof(path),"%s-ownership.json",argv[3]);FILE* out=fopen(path,"wb");if(!out)return 5;
    fprintf(out,"{\"entry\":19965,\"bank\":5,\"calls\":%u,\"commands\":%u,\"frames\":%u,\"pcRanges\":",calls,commandCalls,limit);ranges(out,pcs,0,65536);
    fputs(",\"readRanges\":",out);ranges(out,reads,0,65536);fputs(",\"writeRanges\":",out);ranges(out,writes,0,65536);fputs(",\"apuTargets\":",out);ranges(out,targets,0,65536);
    fputs(",\"allDriverWrites\":",out);ranges(out,driverWrites,0,65536);fputs(",\"gameReads\":",out);ranges(out,gameReads,0,65536);fputs(",\"gameWrites\":",out);ranges(out,gameWrites,0,65536);fputs("}\n",out);fclose(out);
    mCoreConfigDeinit(&core->config);core->deinit(core);return inRoot?6:0;
}
