"""Serial, hidden and muted RetroArch runs after bounded regressions finish.
SPDX-License-Identifier: MPL-2.0. Never changes other processes or system volume.
"""
import json,subprocess,sys
from pathlib import Path

def main():
 r=Path.cwd();d=r/'build-phase7/frontend';d.mkdir(parents=True,exist_ok=False)
 core=r/'build-phase6/final/runtime/mgba_fixed_audio_libretro.dll';bridge=core.with_name('libmgba_mp2k_bridge.dll')
 scenes=json.loads((r/'build-phase3/scenes.json').read_text());script=r/'tools/public-integration/test_phase3_audible.py';rows=[]
 fixture=r/'build-phase4/fixture-AORJ-before-SE/saved-frame-1000-slot-0.rawstate'
 jobs=[]
 for index,inc in enumerate((False,True,False,True)):
  jobs.append((f'AORJ-2x-repeat-{index}',scenes['AORJ']['rom'],fixture,'2','experimental',True,inc))
 jobs.append(('AORJ-native-1x',scenes['AORJ']['rom'],fixture,'1','disabled',True,True))
 for code,scene in scenes.items():jobs.append((code+'-switch-1313',scene['rom'],scene['state'],'1,3,1,3','experimental',False,True))
 for index,(name,rom,state,schedule,mode,single,inc) in enumerate(jobs):
  args=[sys.executable,str(script),'--core',str(core),'--bridge',str(bridge),'--rom',rom,'--state',str(state),'--output',str(d/name),
   '--schedule',schedule,'--segment-seconds','8','--mode',mode,'--port',str(56583+index*2),'--muted']
  if single:args.append('--single-se')
  if inc:args.append('--incremental-log')
  with (d/(name+'.log')).open('xb') as log:proc=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=85)
  rows.append(dict(case=name,returncode=proc.returncode,command=args));print(name,proc.returncode,flush=True)
  (d/'jobs.json').write_text(json.dumps(rows,indent=2)+'\n')
  assert proc.returncode==0,name

if __name__=='__main__':main()
