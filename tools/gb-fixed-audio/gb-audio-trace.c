/* GB0 read-only ROM native bus tracer. No production route or ROM patches. */
#include <mgba/gb/core.h>
#include <mgba/core/core.h>
#include <mgba/core/timing.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba/internal/sm83/decoder.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

struct Writer { unsigned bank,pc,functionPC,count,frames,lastFrame,firstFrame; uint64_t targets; uint16_t stack[6]; uint8_t window[64]; unsigned windowSize; };
static struct Writer writers[4096];
static unsigned writerCount,frame,apuWrites,waveWrites,timerWrites,powerWrites,frequencyWrites,irqCounts[5],firstAudio=UINT_MAX;
static struct GB* gb;
static void (*nativeStore)(struct SM83Core*,uint16_t,int8_t);
static uint16_t (*nativeIRQ)(struct SM83Core*);
static FILE *events,*pcm;
static uint64_t framesSamples;
static FILE* createFile(const char* path) {
    int fd=_open(path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
    return fd<0?NULL:_fdopen(fd,"wb");
}

static uint8_t view(struct SM83Core* c,uint16_t a){return GBView8(c,a,-1);}
static uint16_t word(struct SM83Core* c,uint16_t a){return view(c,a)|view(c,a+1)<<8;}
static unsigned bankAt(uint16_t a){return a<0x4000?gb->memory.currentBank0:a<0x8000?gb->memory.currentBank:65535;}
static uint16_t writerPC(struct SM83Core* c,uint16_t address) {
    /* PC at a store is the exact next-PC. Decode recognized completed stores;
     * unrecognized stores retain next-PC and are labeled as such in results. */
    uint16_t p=c->pc;
    if(view(c,p-3)==0xea && word(c,p-2)==address)return p-3;
    if(view(c,p-2)==0xe0 && (0xff00|view(c,p-1))==address)return p-2;
    if(view(c,p-2)==0x36)return p-2;
    return p-1;
}
static void store(struct SM83Core* c,uint16_t address,int8_t signedValue) {
    uint8_t value=signedValue;
    bool audio=address>=0xff10 && address<=0xff3f;
    bool timer=address>=0xff04 && address<=0xff07;
    if(audio||timer||address==0xff4d) {
        unsigned pc=writerPC(c,address),bank=bankAt(pc);uint8_t old=gb->memory.io[address&127];
        if(audio) {
            ++apuWrites;if(address>=0xff30)++waveWrites;if(address==0xff26)++powerWrites;
            if(address==0xff13||address==0xff14||address==0xff18||address==0xff19||address==0xff1d||address==0xff1e||address==0xff22)++frequencyWrites;
            if(frame>5 && firstAudio==UINT_MAX)firstAudio=frame;
            unsigned id=0;
            for(;id<writerCount;++id)if(writers[id].pc==pc && writers[id].bank==bank)break;
            if(id==writerCount && writerCount<4096) {
                struct Writer* w=&writers[writerCount++];memset(w,0,sizeof(*w));w->pc=pc;w->bank=bank;w->firstFrame=frame;w->lastFrame=UINT_MAX;
                for(unsigned j=0;j<6;++j)w->stack[j]=word(c,c->sp+j*2);
                /* Prefer a stack-backed CALL target encompassing the writer.
                 * This prevents an isolated `ld (c),a; ret` from defining a
                 * whole family. Still heuristic: no full call graph claim. */
                w->functionPC=pc;
                for(unsigned j=0;j<6;++j){
                    unsigned ret=w->stack[j];
                    if(ret>=3 && ret<0x8000 && bankAt(ret-3)==bank && view(c,ret-3)==0xcd){
                        unsigned entry=word(c,ret-2);
                        if(entry<=pc && pc-entry<128){w->functionPC=entry;break;}
                    }
                }
                for(unsigned at=w->functionPC;at<w->functionPC+64;) {
                    int length=SM83InstructionLength(view(c,at));if(length<1||length>3)break;
                    uint8_t op=view(c,at);w->window[w->windowSize++]=op;
                    for(int k=1;k<length;++k) {
                        /* Keep CB subopcode and NRxx constants, normalize other operands. */
                        uint8_t v=view(c,at+k);w->window[w->windowSize++]=op==0xcb||((op==0xe0||op==0xf0) && v>=0x10 && v<=0x3f)?v:0;
                    }
                    at+=length;if(op==0xc9||op==0xd9)break;if(w->windowSize>60)break;
                }
            }
            if(id<4096) {
                struct Writer* w=&writers[id];++w->count;w->targets|=UINT64_C(1)<<(address-0xff10);
                if(w->lastFrame!=frame){++w->frames;w->lastFrame=frame;}
            }
        } else if(timer)++timerWrites;
        if(events) {
            /* This build disables ENABLE_DEBUGGERS, so globalCycles is not
             * accumulated. masterCycles + relativeCycles is the real native
             * bus clock; uint32 wrap is explicit (well beyond this scan). */
            fprintf(events,"{\"frame\":%u,\"cycle\":%u,\"pc\":%u,\"nextPc\":%u,\"bank\":%u,\"sp\":%u,\"stack\":[",frame,(uint32_t)mTimingCurrentTime(&gb->timing),pc,c->pc,bank,c->sp);
            for(unsigned j=0;j<6;++j)fprintf(events,"%s%u",j?",":"",word(c,c->sp+2*j));
            fprintf(events,"],\"address\":%u,\"oldRaw\":%u,\"value\":%u,\"model\":%u,\"doubleSpeed\":%u,\"ime\":%d,\"irqPending\":%d,\"ie\":%u,\"if\":%u,\"div\":%u,\"apuFrame\":%u}\n",address,old,value,gb->model,gb->doubleSpeed,gb->memory.ime,c->irqPending,gb->memory.ie,gb->memory.io[GB_REG_IF],gb->timer.internalDiv,gb->audio.frame);
        }
    }
    nativeStore(c,address,signedValue);
}
static uint16_t irq(struct SM83Core* c) {
    uint16_t v=nativeIRQ(c);if(v>=0x40 && v<=0x60 && !(v&7))++irqCounts[(v-0x40)/8];return v;
}
int main(int argc,char** argv) {
    if(argc<4){fprintf(stderr,"gb-audio-trace ROM frames output-prefix [input=0|1] [detail=0|1]\n");return 2;}
    unsigned frames=strtoul(argv[2],NULL,10);bool input=argc>4 && atoi(argv[4]),detail=argc>5 && atoi(argv[5]);
    struct mCore* core=GBCoreCreate();if(!core||!core->init(core))return 3;
    mCoreInitConfig(core,NULL);
    mCoreConfigSetValue(&core->config,"gb.model","DMG");mCoreConfigSetValue(&core->config,"sgb.model","DMG");mCoreConfigSetValue(&core->config,"cgb.hybridModel","DMG");mCoreConfigSetValue(&core->config,"cgb.sgbModel","DMG");
    core->opts.frameskip=9;core->opts.volume=0x100;core->loadConfig(core,&core->config);
    struct VFile* rom=VFileOpen(argv[1],O_RDONLY);if(!rom||!core->loadROM(core,rom))return 4;
    /* No autoload save, save-file paths, patches, cheats, or default config. */
    core->reset(core);core->setAudioBufferSize(core,4096);gb=core->board;
    nativeStore=gb->cpu->memory.store8;gb->cpu->memory.store8=store;nativeIRQ=gb->cpu->irqh.irqVector;gb->cpu->irqh.irqVector=irq;
    char path[2048];
    if(detail){snprintf(path,sizeof(path),"%s-events.jsonl",argv[3]);events=createFile(path);snprintf(path,sizeof(path),"%s.s16le",argv[3]);pcm=createFile(path);if(!events||!pcm)return 5;}
    int16_t buffer[8192];
    for(frame=0;frame<frames;++frame) {
        /* Generic exploratory pulses. NO_INPUT scan is always separate. */
        uint32_t keys=input && frame>=180 && frame%120<3?8:input && frame>=240 && frame%120==30?1:0;
        core->setKeys(core,keys);core->runFrame(core);
        size_t n=mAudioBufferRead(core->getAudioBuffer(core),buffer,4096);framesSamples+=n;if(pcm)fwrite(buffer,4,n,pcm);
    }
    snprintf(path,sizeof(path),"%s-summary.json",argv[3]);FILE* out=createFile(path);if(!out)return 6;
    fprintf(out,"{\"frames\":%u,\"model\":%u,\"doubleSpeed\":%u,\"sampleRate\":%u,\"samples\":%llu,\"apuWrites\":%u,\"waveWrites\":%u,\"timerWrites\":%u,\"powerWrites\":%u,\"frequencyWrites\":%u,\"firstAudioFrame\":%u,\"irqCounts\":[%u,%u,%u,%u,%u],\"writers\":[",frames,gb->model,gb->doubleSpeed,core->audioSampleRate(core),(unsigned long long)framesSamples,apuWrites,waveWrites,timerWrites,powerWrites,frequencyWrites,firstAudio,irqCounts[0],irqCounts[1],irqCounts[2],irqCounts[3],irqCounts[4]);
    for(unsigned i=0;i<writerCount;++i) {
        struct Writer* w=&writers[i];fprintf(out,"%s{\"pc\":%u,\"functionPC\":%u,\"bank\":%u,\"count\":%u,\"frames\":%u,\"firstFrame\":%u,\"lastFrame\":%u,\"targets\":\"%012llx\",\"stack\":[",i?",":"",w->pc,w->functionPC,w->bank,w->count,w->frames,w->firstFrame,w->lastFrame,(unsigned long long)w->targets);
        for(unsigned j=0;j<6;++j)fprintf(out,"%s%u",j?",":"",w->stack[j]);fputs("],\"normalizedWindow\":\"",out);for(unsigned j=0;j<w->windowSize;++j)fprintf(out,"%02x",w->window[j]);fputs("\"}",out);
    }
    fputs("]}\n",out);fclose(out);if(events)fclose(events);if(pcm)fclose(pcm);
    mCoreConfigDeinit(&core->config);core->deinit(core);return 0;
}
