"""Bounded private OBS game-window + Windows-output recording; no fake speed.
SPDX-License-Identifier: MPL-2.0. Uses a new profile/collection, never old recordings.
"""
import argparse,configparser,json,os,shutil,subprocess,sys,time
from pathlib import Path
from test_three_x_poc import sha
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',choices=['AFXJ','AORJ'],required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--window-title',required=True,help='Exact observed mGBA window title, never a generic RetroArch executable match')
 p.add_argument('--profile-suffix',default='matched');a=p.parse_args()
 root=Path.cwd();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 # Do not interfere with a user's already-running OBS instance.
 running=subprocess.check_output(['tasklist','/FI','IMAGENAME eq obs64.exe'],creationflags=subprocess.CREATE_NO_WINDOW)
 assert b'obs64.exe' not in running.lower(),'Existing OBS session: leave it untouched'
 template=Path(os.environ['APPDATA'])/'obs-studio'
 portable=root/('build-phase10/obs-portable-'+a.game+'-'+a.profile_suffix)
 if not portable.exists():
  (portable/'bin/64bit').mkdir(parents=True)
  for f in Path('D:/OBS/bin/64bit').iterdir():
   if f.is_file():shutil.copy2(f,portable/'bin/64bit'/f.name)
  for n in ('data','obs-plugins'):
   os.symlink('D:/OBS/'+n,portable/n,target_is_directory=True)
 # Qt platform/image plugins are directories beside the installed executable.
 for f in Path('D:/OBS/bin/64bit').iterdir():
  dest=portable/'bin/64bit'/f.name
  if f.is_dir() and not dest.exists():shutil.copytree(f,dest)
 base=portable/'config/obs-studio';base.mkdir(parents=True,exist_ok=True)
 for n in ('global.ini','user.ini'):
  if not (base/n).exists():
   cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str;cfg.read(template/n,encoding='utf-8-sig')
   if cfg.has_section('Locations'):cfg.remove_section('Locations')
   with (base/n).open('x',encoding='utf8') as f:cfg.write(f,space_around_delimiters=False)
 name='V04Phase10Private_'+a.game+'_'+a.profile_suffix
 prof=base/'basic/profiles'/name;collection=base/'basic/scenes'/(name+'.json')
 assert not prof.exists() and not collection.exists(),'Use a fresh recording destination'
 prof.mkdir(parents=True);collection.parent.mkdir(parents=True,exist_ok=True);c=configparser.ConfigParser(interpolation=None);c.optionxform=str
 c.read(template/'basic/profiles/V03TechnicalDemo20261009/basic.ini',encoding='utf-8-sig')
 c['General']['Name']=name;c['SimpleOutput'].update(FilePath=str(out),RecFormat2='mkv',RecQuality='Small',RecEncoder='x264',ABitrate='320',RecTracks='1')
 c['Output']['FilenameFormatting']='v04-private-%CCYY-%MM-%DD_%hh-%mm-%ss'
 with (prof/'basic.ini').open('x',encoding='utf8') as f:c.write(f,space_around_delimiters=False)
 scene=json.loads((template/'basic/scenes/V03TechnicalDemo20261009.json').read_text(encoding='utf-8-sig'));scene['name']=name
 for k in list(scene):
  if 'AudioDevice' in k and k!='DesktopAudioDevice1':del scene[k]
 scene['DesktopAudioDevice1'].update(mixers=1,volume=1.0,muted=False,sync=0,filters=[])
 scene['DesktopAudioDevice1']['settings']={'device_id':'default','use_device_timing':True}
 for s in scene['sources']:
  if s['id']=='window_capture':s['settings'].update(window=a.window_title+':RetroArch:retroarch.exe',priority=0,method=2,cursor=False,client_area=True,capture_audio=False)
  if s['id'].startswith('text_'):s['settings'].update(text='Private v0.4 RC / HUMAN_REVIEW_REQUIRED',read_from_file=False)
 scene['modules']['output-timer'].update(recordTimerSeconds=38,autoStartRecordTimer=True,pauseRecordTimer=False)
 collection.write_text(json.dumps(scene,ensure_ascii=False,indent=2),encoding='utf8');(out/'obs-scene.json').write_bytes(collection.read_bytes())
 global_file=base/'global.ini';original=global_file.read_bytes();(out/'obs-global-before.ini').write_bytes(original)
 sentinel=base/'.sentinel';old_sessions={p.name for p in sentinel.glob('*') if p.is_file()}
 obs=None
 try:
  si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
  with (out/'obs-process.log').open('xb') as log:
   obs=subprocess.Popen([str(portable/'bin/64bit/obs64.exe'),'--portable','--profile',name,'--collection',name,'--disable-updater','--startrecording','--minimize-to-tray'],cwd=portable/'bin/64bit',stdout=log,stderr=subprocess.STDOUT,startupinfo=si)
  deadline=time.perf_counter()+20
  while not list(out.glob('*.mkv')) and time.perf_counter()<deadline:time.sleep(.05)
  files=list(out.glob('*.mkv'));assert len(files)==1,'OBS did not start recording'
  recording=files[0];detected=time.perf_counter()
  cmd=[sys.executable,str(root/'tools/public-integration/test_v04_rc_frontend.py'),'--core',str(root/'build-phase9/final/runtime/mgba_fixed_audio_libretro.dll'),'--bridge',str(root/'build-phase9/final/runtime/libmgba_mp2k_bridge.dll'),'--adapter',str(root/'build-phase10/rc_frontend_adapter_libretro.dll'),'--output',str(out/'frontend'),'--codes',a.game,'--seconds',str(25/6),'--audible-video']
  with (out/'frontend-batch.log').open('xb') as log:result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=60)
  # Allow OBS's output timer to close the container before stopping only our process.
  while time.perf_counter()-detected<40 and obs.poll() is None:time.sleep(.1)
  (out/'recording.json').write_text(json.dumps(dict(game=a.game,recording=recording.name,obs_creation_detected_perf=detected,creation_poll_seconds=.05,frontend_returncode=result.returncode,command=cmd,
   capture='Actual RetroArch D3D11 window plus default Windows WASAPI output; no microphones or application-audio duplication. Independent WASAPI reference also recorded.',
   expected_window_title=a.window_title,human_review='HUMAN_REVIEW_REQUIRED',video_speed_editing=False,audio_time_stretch=False),indent=2)+'\n',encoding='utf8')
  assert result.returncode==0
 finally:
  if obs is not None and obs.poll() is None:obs.terminate();obs.wait(timeout=5)
  # Preserve existing profile selection/settings; leave new private profile as evidence.
  global_file.write_bytes(original)
  # Preserve our stopped portable-session markers as evidence, so repeated
  # private captures do not leave a stale-session prompt. Never move old markers.
  stopped=out/'own-obs-session-markers';stopped.mkdir()
  for p in sentinel.glob('*'):
   if p.is_file() and p.name not in old_sessions:shutil.move(str(p),stopped/p.name)
 print('Private recording finished:',out)
if __name__=='__main__':main()
