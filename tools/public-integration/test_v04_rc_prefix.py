"""Fresh bounded RC PCM/event/initial-RAM checks against retained Phase9 scenes.
SPDX-License-Identifier: MPL-2.0. Never updates golden/reference data.
"""
import argparse,json,subprocess,sys
from pathlib import Path
from test_three_x_poc import sha
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('core','bridge','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();root=Path.cwd();a.output.mkdir(parents=True,exist_ok=False)
 scenes=json.loads((root/'build-phase3/scenes.json').read_text(encoding='utf8'));checks=[]
 for code,scene in scenes.items():
  out=a.output/code
  cmd=[sys.executable,str(Path(__file__).with_name('test_phase2_scene.py')),'--runner',str(root/'build-phase6/retro-runner.exe'),'--core',str(a.core.resolve()),'--bridge',str(a.bridge.resolve()),'--rom',scene['rom'],'--state',scene['state'],'--output',str(out.resolve()),'--seconds','4','--speeds','1,2,3']
  with (a.output/(code+'.log')).open('xb') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=90)
  assert r.returncode==0
  new=json.loads((out/'results.json').read_text(encoding='utf8'))['records']
  oldroot=root/'build-phase9/scenes16'/('phase4-'+code);old=json.loads((oldroot/'results.json').read_text(encoding='utf8'))['records']
  for n,o in zip(new,old):
   speed=n['speed'];b=(out/str(speed)/'callback.s16le').read_bytes();ref=(oldroot/str(speed)/'callback.s16le').read_bytes()
   row=dict(game=code,speed=speed,frames=len(b)//4,pcm_prefix_exact=ref[:len(b)]==b,event_prefix_exact=o['events'][:len(n['events'])]==n['events'],initial_ram_exact=o['initial_ram_sha256']==n['initial_ram_sha256'],underrun=n['underrun'],overrun=n['overrun'],pcm_sha256=sha(out/str(speed)/'callback.s16le'))
   checks.append(row);assert row['pcm_prefix_exact'] and row['event_prefix_exact'] and row['initial_ram_exact'] and not row['underrun'] and not row['overrun'],row
  print(code,'3 fresh prefix checks PASS',flush=True)
 (a.output/'results.json').write_text(json.dumps(dict(passed=True,core_sha256=sha(a.core),bridge_sha256=sha(a.bridge),checks=checks,scope='Fresh4-second prefixes; complete48/16/12suite/36 results separately retained and verified. Not a rerun of those full suites.'),indent=2)+'\n',encoding='utf8')
if __name__=='__main__':main()
