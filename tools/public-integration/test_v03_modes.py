"""Exercise v0.3 modes and unverified EWRAM fixtures with isolated state/audio outputs.
SPDX-License-Identifier: MPL-2.0. ROMs are read-only; hashes verified before and after.
No ACTIVE-only result is an audio PASS or database promotion.
"""
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path
import numpy as np
from scipy.signal import correlate
def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
def audio_compare(a,b):
 x=np.fromfile(a,dtype='<i2').reshape(-1,2).astype(float).mean(axis=1)[::16]
 y=np.fromfile(b,dtype='<i2').reshape(-1,2).astype(float).mean(axis=1)[::16]
 # Compare one energetic second of 1x against 2x at the SAME sample scale.
 # This bounds scope to a waveform segment; scene/event coverage is not inferred.
 width=2048
 starts=[i for i in range(0,min(len(x)-width,2048*12),width) if np.dot(x[i:i+width],x[i:i+width])>width*400]
 if not starts or len(y)<width:return dict(comparison='NO_AUDIBLE_SEGMENT',correlation=None)
 start=starts[0];segment=x[start:start+width];segment-=segment.mean()
 y=y[:2048*35];y-=y.mean()
 dot=correlate(y,segment,mode='valid',method='fft')
 energy=np.convolve(y*y,np.ones(width),mode='valid')
 scores=dot/np.sqrt(np.maximum(energy,1)*np.dot(segment,segment))
 at=int(np.argmax(scores))
 return dict(comparison='SAME_SAMPLE_RATE_SEGMENT',correlation=round(float(scores[at]),6),
  reference_start_sample=start*16,comparison_start_sample=at*16,samples=width*16,
  scope='One energetic 1-second boot/play waveform segment, mono downsampled 16x; no all-scene BGM/SE assurance.')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('root','rom-dir','runner','core','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--resume',action='store_true')
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=a.resume)
 pin=a.output/'core-sha256.txt'
 if pin.exists():assert pin.read_text()==digest(a.core),'Core changed; use new output directory'
 else:pin.write_text(digest(a.core))
 evidence=json.loads((a.root/'compatibility/sources/local-scanner-evidence.json').read_text(encoding='utf8'))['records']
 files={}
 for path in a.rom_dir.glob('*.gba'):
  with path.open('rb') as f:header=f.read(0xC0)
  if len(header)>=0xC0:files.setdefault(header[0xAC:0xB0].decode('ascii',errors='replace'),[]).append(path)
 def rom(code):
  row=next(r for r in evidence if r['game_code']==code)
  path=next(x for x in files[code] if digest(x)==row['sha256'])
  return path,row
 env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
 env['PATH']=str(a.core.parent)+';C:/msys64/mingw64/bin;'+env['PATH']
 observations=[];cases=[]
 schedule=a.output/'inputs.txt'
 schedule.write_text(''.join(f'{f} {button} 3\n' for f in range(300,5500,180) for button in ('START','A')))
 def run(code,name,mode='experimental',speed='normal',frames=6000,extra=None,controls=schedule):
  path,row=rom(code);d=a.output/name;d.mkdir(exist_ok=a.resume)
  local=dict(env,LIBRETRO_SYSTEM_DIRECTORY=str(d),MGBA_RUNNER_CALLBACK_PATH=str(d/'callback.s16le'),MGBA_FIXED_AUDIO_RUN_TRACE='1')
  if mode:local['MGBA_RUNNER_FIXED_AUDIO_MODE']=mode
  if extra:local.update(extra)
  started=time.perf_counter()
  try:
   result=subprocess.CompletedProcess([],0) if a.resume and (d/'run.log').exists() else subprocess.run([str(a.runner),str(a.core),str(path),str(d/'run'),speed,str(frames),str(controls)],env=local,capture_output=True,timeout=120)
   log=(d/'run.log').read_text(errors='replace') if (d/'run.log').exists() else ''
   outcome='Crash' if result.returncode else 'Fixed Audio ACTIVE' if ('FIXED AUDIO ACTIVE' in log or '[B6JJ AUDIO] backend=' in log) else 'Fallback'
  except subprocess.TimeoutExpired:log='';outcome='Hang'
  assert digest(path)==row['sha256'],'ROM changed'
  fixed=[x for x in log.splitlines() if 'FIXED AUDIO ACTIVE' in x]
  final=next((x for x in reversed(log.splitlines()) if '[FIXED AUDIO] end ' in x),'')
  case=dict(game_code=code,name=name,mode=mode or 'default',speed=speed,result=outcome,
    seconds=round(time.perf_counter()-started,3),final=final,sha256=row['sha256'])
  cases.append(case);print(name,outcome,flush=True)
  return d,case,log
 # Default and explicit mode use sibling bridge discovery without any feature/bridge env.
 d,case,log=run('AAMJ','default',mode=None,frames=1200)
 assert case['result']=='Fixed Audio ACTIVE'
 _,case,_=run('AAMJ','experimental',frames=1200);assert case['result']=='Fixed Audio ACTIVE'
 _,case,_=run('AAMJ','conservative',mode='conservative',frames=1200);assert case['result']=='Fixed Audio ACTIVE'
 disabled_extra=dict(MGBA_FIXED_AUDIO_TEMPO='1',MGBA_FIXED_AUDIO_EWRAM_EXPERIMENTAL_ALL='1',MGBA_FIXED_AUDIO_PROTOTYPE='1',MGBA_FIXED_AUDIO_CLOCK_TONE='1')
 _,case,log=run('AAMJ','disabled-env-conflict',mode='disabled',frames=1200,extra=disabled_extra)
 assert case['result']=='Fallback' and '[AUDIO CLOCK] enabled' not in log and '[FIXED AUDIO] end' not in log
 _,case,_=run('AAMJ','legacy-off',mode=None,frames=1200,extra={'MGBA_FIXED_AUDIO_TEMPO':'0'});assert case['result']=='Fallback'
 _,case,_=run('AAMJ','legacy-option-off',frames=1200,extra={'MGBA_RUNNER_LEGACY_FIXED_AUDIO':'OFF','MGBA_FIXED_AUDIO_EWRAM_EXPERIMENTAL_ALL':'1'});assert case['result']=='Fallback'
 for code in ('A2NJ','A2QJ','A2VJ'):
  # These exact identities were outside the safe allowlist before v0.3.
  records=[]
  for speed in ('normal','ff','switch'):
   d,c,log=run(code,code+'-'+speed,speed=speed)
   records.append((d,c,log))
  _,c,_=run(code,code+'-conservative',mode='conservative',frames=1200,extra={'MGBA_FIXED_AUDIO_EWRAM_EXPERIMENTAL_ALL':'1'})
  assert c['result']=='Fallback'
  _,c,_=run(code,code+'-disabled',mode='disabled',frames=1200);assert c['result']=='Fallback'
  comparison=audio_compare(records[0][0]/'callback.s16le',records[1][0]/'callback.s16le')
  active=all(c['result']=='Fixed Audio ACTIVE' for _,c,_ in records)
  observations.append(dict(sha256=records[0][1]['sha256'],game_code=code,result='Fixed Audio ACTIVE' if active else 'Fallback',
   verification_status='ACTIVE_WITH_LIMITED_WAVEFORM_COMPARISON' if active else 'NATIVE_FALLBACK_OBSERVED',
   notes='v0.3: boot/play 6000-frame normal/2x/switch probes. ACTIVE is not an audio PASS; no database promotion. Waveform comparison is limited to one segment.',
   audio_comparison=comparison,runs=[c for _,c,_ in records]))
 # An unverified ROM_PLAYER and a known unsupported game use the same DLL.
 _,c,log=run('AG7J','ROM-player-unverified',speed='ff',frames=2400)
 assert 'native-pending-validation' in log
 observations.append(dict(sha256=c['sha256'],game_code='AG7J',result=c['result'],verification_status='NATIVE_FALLBACK_OBSERVED',
  notes='v0.3: ROM_PLAYER trial initialized but no live activation occurred in the 2400-frame 2x boot scenario. Native audio remained active; this is not a permanent unsupported verdict.'))
 _,c,_=run('A8CJ','ROM-player-active',speed='ff',frames=2400);assert c['result']=='Fixed Audio ACTIVE'
 fallback,_,_=run('BVGJ','unknown-experimental',frames=2400)
 off,_,_=run('BVGJ','unknown-disabled',mode='disabled',frames=2400)
 assert digest(fallback/'callback.s16le')==digest(off/'callback.s16le')
 result=dict(passed=all(c['result'] not in ('Crash','Hang') for c in cases),cases=cases,records=observations,
  scope='Mode/reload checks, boot/play and one-segment automatic waveform comparisons only. Unverified identities remain unverified.',
  saves='ISOLATED_TEMPORARY_OUTPUT',roms='READ_ONLY_HASH_UNCHANGED',core_sha256=digest(a.core))
 (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 assert result['passed']
if __name__=='__main__':main()
