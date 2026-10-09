"""Windows v0.3 RC scenario/audio/throughput tests. SPDX-License-Identifier: MPL-2.0.
Exact read-only ROM identities; state/audio output isolated. Never promotes ACTIVE to audio PASS.
Throughput is unthrottled headroom, not a live frontend speed guarantee.
"""
import argparse,ctypes,hashlib,json,os,re,subprocess,time
from ctypes import wintypes
from pathlib import Path
import numpy as np
from scipy.signal import correlate,resample_poly
FPS=59.727501
CODES=['A2NJ','A2QJ','A2VJ','AE2J','ABFJ','BGXJ','A8CJ','A2CJ','ACHJ','AORJ','ASTJ','U3IJ','AG7J']
def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
def cpu_seconds(p):
 v=[wintypes.FILETIME() for _ in range(4)]
 if not ctypes.windll.kernel32.GetProcessTimes(wintypes.HANDLE(int(p._handle)),*[ctypes.byref(x) for x in v]):return None
 return sum((x.dwHighDateTime<<32)+x.dwLowDateTime for x in v[2:])/1e7
def comparisons(a,b):
 x=np.fromfile(a,dtype='<i2').reshape(-1,2).astype(float).mean(axis=1)[::16]
 y=np.fromfile(b,dtype='<i2').reshape(-1,2).astype(float).mean(axis=1)[::16]
 width=2048;y=y[:width*90];scores=[]
 for start in range(0,min(len(x)-width,width*60),width*5):
  seg=x[start:start+width].copy();seg-=seg.mean()
  if np.dot(seg,seg)<width*400 or len(y)<width:continue
  yy=y-y.mean();dots=correlate(yy,seg,mode='valid',method='fft')
  energies=np.convolve(yy*yy,np.ones(width),mode='valid')
  values=dots/np.sqrt(np.maximum(energies,1)*np.dot(seg,seg));at=int(np.argmax(values))
  matched=yy[at:at+width];win=np.hanning(width)
  sx=abs(np.fft.rfft(seg*win));sy=abs(np.fft.rfft(matched*win))
  spectral=float(np.dot(sx,sy)/max(np.linalg.norm(sx)*np.linalg.norm(sy),1))
  # Test same-sample-scale agreement against a doubled or halved reference.
  alternatives={}
  for up,down,label in [(1,2,'half_duration'),(2,1,'double_duration')]:
   ss=resample_poly(seg,up,down);dots2=correlate(yy,ss,mode='valid',method='fft')
   en=np.convolve(yy*yy,np.ones(len(ss)),mode='valid')
   alternatives[label]=round(float(np.max(dots2/np.sqrt(np.maximum(en,1)*np.dot(ss,ss)))),6)
  scores.append(dict(reference_sample=start*16,comparison_sample=at*16,samples=width*16,
   same_scale_correlation=round(float(values[at]),6),spectral_cosine=round(spectral,6),alternative_duration_correlations=alternatives))
  if len(scores)==5:break
 return dict(status='HUMAN_REVIEW_REQUIRED',method='Up to five non-silent one-second mono/16x waveform windows at identical sample scale; aligned FFT magnitude and half/double-duration controls. Boot/native lead-in and game scene differences remain confounders.',
  windows=scores,automated_observation='SAME_SCALE_AGREEMENT' if scores and sum(s['same_scale_correlation']>=.8 and s['spectral_cosine']>=.9 for s in scores)>=min(3,len(scores)) else 'INCONCLUSIVE',
  full_bgm_se_verified=False,pitch_and_tempo_certified=False)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('root','rom-dir','runner','core','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--group',choices=['audio','performance'],required=True)
 p.add_argument('--codes',help='Comma-separated exact game-code selection for extra scenarios')
 p.add_argument('--only-unverified',action='store_true',help='Require an Experimental Unverified release; code alone cannot select a revision')
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 db=json.loads((a.root/'compatibility/gba-compatibility.json').read_text(encoding='utf8'))
 index={}
 for f in a.rom_dir.glob('*.gba'):
  with f.open('rb') as stream:h=stream.read(0xc0)
  if len(h)>=0xc0:index.setdefault(h[0xac:0xb0].decode('ascii',errors='replace'),[]).append(f)
 identities={}
 for code in (a.codes.split(',') if a.codes else CODES if a.group=='audio' else ['AAMJ','A2QJ','A8CJ']):
  rows=[r for r in db['records'] if r['game_code']==code and r.get('sha256')]
  if a.only_unverified:rows=[r for r in rows if r['public_status']=='EXPERIMENTAL_UNVERIFIED']
  found=None
  for row in rows:
   for f in index.get(code,[]):
    if digest(f)==row['sha256']:found=(row,f);break
   if found:break
  assert found,'Exact selected identity unavailable: '+code
  identities[code]=found
 clean={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
 clean['PATH']=str(a.core.parent)+os.pathsep+clean['PATH']
 control=a.output/'controls.txt'
 control.write_text(''.join(f'{f} {button} 3\n' for f in range(300,8500,180) for button in ('START','A')),encoding='utf8')
 actions=a.output/'recovery.txt'
 actions.write_text('2000 SAVE 0\n2100 SAVE 1\n3000 LOAD 0\n5000 REWIND 1\n5020 LOAD 1\n5040 LOAD 0\n5060 REWIND 0\n',encoding='utf8')
 results=[];allruns=[]
 def run(code,label,mode,speed,frames,recovery=False,capture=False):
  row,rom=identities[code];d=a.output/label;d.mkdir()
  env=dict(clean,LIBRETRO_SYSTEM_DIRECTORY=str(d),MGBA_RUNNER_FIXED_AUDIO_MODE=mode)
  if capture:env.update(MGBA_RUNNER_CALLBACK_PATH=str(d/'callback.s16le'),MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1')
  if recovery:env['MGBA_RUNNER_STATE_ACTIONS']=str(actions)
  t=time.perf_counter();status='Other Failure'
  with (d/'process.log').open('wb') as log:
   proc=subprocess.Popen([str(a.runner),str(a.core),str(rom),str(d/'run'),speed,str(frames),str(control)],env=env,stdout=log,stderr=subprocess.STDOUT)
   try:code_exit=proc.wait(timeout=900);cpu=cpu_seconds(proc);status=('Crash' if code_exit<0 or code_exit>=0x80000000 else 'Other Failure') if code_exit else 'Completed'
   except subprocess.TimeoutExpired:proc.kill();proc.wait();cpu=cpu_seconds(proc);status='Hang'
  wall=time.perf_counter()-t
  text=(d/'run.log').read_text(errors='replace') if (d/'run.log').exists() else ''
  active='FIXED AUDIO ACTIVE' in text
  diagnostics=[s for s in text.splitlines() if 'candidate_active=' in s]
  last_active=re.search(r'candidate_active=(\d+)',diagnostics[-1]) if diagnostics else None
  final=next((s for s in reversed(text.splitlines()) if '[FIXED AUDIO] end ' in s),'')
  counters={k:int(v) for k,v in re.findall(r'\b(candidate|native|callback|underrun|overrun|fallback)=(\d+)',final)}
  fallback=list(dict.fromkeys(re.findall(r'(?:SAFE FALLBACK reason=|NATIVE_FALLBACK reason=)([A-Z0-9_]+)',text)))
  songstarts=re.findall(r'\[MP2K EVENT\].*SONG_START s=(\d+) p=(\d+)',text)
  state=re.findall(r'\[HOST\] state ([A-Z_]+) slot=(\d+) frame=(\d+) success=(\d+)',text)
  record=dict(game_code=code,label=label,mode=mode,speed=speed,frames=frames,outcome=status,
   runtime='Fixed Audio ACTIVE' if active else 'Fallback',activation_observed=active,
   final_audio_route=('FIXED' if last_active[1]=='1' else 'NATIVE') if last_active else 'NO_FINAL_DIAGNOSTIC',
   wall_seconds=round(wall,4),cpu_seconds=round(cpu,4) if cpu is not None else None,
   one_core_cpu_percent=round(cpu/wall*100,2) if cpu is not None else None,unthrottled_emulated_speed_x=round(frames/FPS/wall,3),
   target_speed_capacity_percent=round(frames/FPS/wall/(2 if speed=='ff' else 1)*100,2),
   counters=counters,fallback_reasons=fallback,distinct_song_player_starts=len(set(songstarts)),
   observed_players=sorted({int(p) for s,p in songstarts}),state_calls=state,
   state_load_api_success=all(s[3]=='1' for s in state if s[0]=='LOAD') if recovery else None,
   rewind_api_success=all(s[3]=='1' for s in state if s[0]=='REWIND') if recovery else None,
   recovery_events=len(re.findall(r'STATE_LOAD_RECOVERED|REWIND_RECOVERED',text)),core_sha256=digest(a.core))
  assert digest(rom)==row['sha256'],'ROM changed'
  allruns.append(record);print(label,record['runtime'],status,'wall',round(wall,2),'guards',fallback,flush=True)
  return d,record
 if a.group=='audio':
  for code,(row,rom) in identities.items():
   runs=[]
   for speed in ('normal','ff','switch'):
    d,r=run(code,code+'-'+speed,'experimental',speed,9000,capture=True);runs.append((d,r))
   d,r=run(code,code+'-recovery','experimental','normal',9000,recovery=True,capture=True);runs.append((d,r))
   comparison=comparisons(runs[0][0]/'callback.s16le',runs[1][0]/'callback.s16le')
   results.append(dict(game_code=code,title=row['title'],sha256=row['sha256'],driver_family=row['driver_family'],
    original_public_status=row['public_status'],runtime_eligibility=row['runtime_eligibility'],
    runs=[r for d,r in runs],audio=comparison,bgm='HUMAN_REVIEW_REQUIRED',se='HUMAN_REVIEW_REQUIRED',
    input_scope='START/A boot/menu probes; distinct song/player events are evidence of commands, not proof of audible BGM or SE correctness.',
    public_promotion=False))
   (a.output/'results.json').write_text(json.dumps(dict(core_sha256=digest(a.core),records=results),indent=2)+'\n',encoding='utf8')
 else:
  # Serialized measurements; rotate mode order across repetitions to limit ordering bias.
  modes=['experimental','conservative','disabled']
  for code in identities:
   for repeat in range(3):
    for mode in modes[repeat:]+modes[:repeat]:
     for speed in ('normal','ff'):run(code,f'{code}-{mode}-{speed}-{repeat}',mode,speed,3000)
  for mode in modes:run('A2QJ','long-'+mode,mode,'ff',108000)
  (a.output/'results.json').write_text(json.dumps(dict(core_sha256=digest(a.core),runs=allruns,method='Serialized unthrottled deterministic host, three repetitions per 3000-frame condition; 108000-frame (30.1 emulated minutes) 2x stress per mode. CPU is process kernel+user time divided by wall time; speed metrics are headroom, not live frontend timing. Disk/native capture overhead is included; no physical-device underrun inference.'),indent=2)+'\n')
 assert all(r['outcome']=='Completed' for r in allruns),'Some scenarios crashed/hung; review results'
if __name__=='__main__':main()
