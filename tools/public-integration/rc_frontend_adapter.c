/* SPDX-License-Identifier: MPL-2.0
 * Private frontend-test adapter. Not a replacement emulator; never packaged.
 * Forwards the unchanged candidate core and uses RetroArch's documented
 * SET_FASTFORWARDING_OVERRIDE. GET_THROTTLE_STATE remains the real frontend's.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "libretro.h"
static HMODULE dll;
static retro_environment_t frontend;
static LARGE_INTEGER freq,checked;
static int speed=1;
static void load(void) {
 if(dll)return;
 const char *p=getenv("MGBA_RC_ACTUAL_CORE");
 if(!p || !(dll=LoadLibraryA(p))) {fprintf(stderr,"Private adapter cannot load actual core\n");abort();}
 QueryPerformanceFrequency(&freq);
}
#define API __declspec(dllexport)
#define FORWARD_VOID(name,args,values) API void name args {load();((void (*) args)GetProcAddress(dll,#name)) values;}
#define FORWARD(ret,name,args,values) API ret name args {load();return ((ret (*) args)GetProcAddress(dll,#name)) values;}
API void retro_set_environment(retro_environment_t cb){load();frontend=cb;((void (*)(retro_environment_t))GetProcAddress(dll,"retro_set_environment"))(cb);}
API void retro_run(void){
 load();LARGE_INTEGER now;QueryPerformanceCounter(&now);
 if(now.QuadPart-checked.QuadPart>freq.QuadPart/30){
  checked=now;const char *p=getenv("MGBA_RC_SPEED_FILE");FILE *f=p?fopen(p,"rb"):NULL;int next=0;
  if(f){if(fscanf(f,"%d",&next)!=1)next=0;fclose(f);}
  if((next==1 || next==2 || next==3) && next!=speed){
   struct retro_fastforwarding_override o={(float)next,next>1,false,true};
   bool accepted=frontend(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE,&o);
   fprintf(stderr,"[PRIVATE RC ADAPTER] speed=%d accepted=%d qpc=%lld\n",next,accepted,now.QuadPart);fflush(stderr);
   if(accepted)speed=next;
  }
 }
 ((void (*)(void))GetProcAddress(dll,"retro_run"))();
}
FORWARD_VOID(retro_init,(void),())
FORWARD_VOID(retro_deinit,(void),())
FORWARD(unsigned,retro_api_version,(void),())
FORWARD_VOID(retro_get_system_info,(struct retro_system_info *p),(p))
FORWARD_VOID(retro_get_system_av_info,(struct retro_system_av_info *p),(p))
FORWARD_VOID(retro_set_video_refresh,(retro_video_refresh_t p),(p))
FORWARD_VOID(retro_set_audio_sample,(retro_audio_sample_t p),(p))
FORWARD_VOID(retro_set_audio_sample_batch,(retro_audio_sample_batch_t p),(p))
FORWARD_VOID(retro_set_input_poll,(retro_input_poll_t p),(p))
FORWARD_VOID(retro_set_input_state,(retro_input_state_t p),(p))
FORWARD_VOID(retro_set_controller_port_device,(unsigned p,unsigned d),(p,d))
FORWARD_VOID(retro_reset,(void),())
FORWARD(size_t,retro_serialize_size,(void),())
FORWARD(bool,retro_serialize,(void *p,size_t n),(p,n))
FORWARD(bool,retro_unserialize,(const void *p,size_t n),(p,n))
FORWARD_VOID(retro_cheat_reset,(void),())
FORWARD_VOID(retro_cheat_set,(unsigned i,bool e,const char *p),(i,e,p))
FORWARD(bool,retro_load_game,(const struct retro_game_info *p),(p))
FORWARD(bool,retro_load_game_special,(unsigned t,const struct retro_game_info *p,size_t n),(t,p,n))
FORWARD_VOID(retro_unload_game,(void),())
FORWARD(unsigned,retro_get_region,(void),())
FORWARD(void*,retro_get_memory_data,(unsigned i),(i))
FORWARD(size_t,retro_get_memory_size,(unsigned i),(i))
