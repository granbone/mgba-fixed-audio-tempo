"""Serial real RetroArch RC switches. Private adapter; original candidate DLL.
SPDX-License-Identifier: MPL-2.0. No display/audio unless explicitly requested.
"""
import argparse,json,subprocess,sys
from pathlib import Path
from test_gba_bgm_frontend_phase9 import measure
from test_three_x_poc import sha
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('core','bridge','adapter','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--codes',default='AAMJ,AFXJ,AORJ,B6JJ');p.add_argument('--seconds',type=float,default=8)
 p.add_argument('--audible-video',action='store_true');a=p.parse_args();root=Path.cwd()
 m=json.loads((root/'build-phase9/frontend-baseline-inputs/manifest.json').read_text(encoding='utf8'))['rows']
 f=json.loads((root/'build-phase9/frontend-baseline-inputs/fixtures.json').read_text(encoding='utf8'))['rows']
 a.output.mkdir(parents=True,exist_ok=False);results=dict(core_sha256=sha(a.core),bridge_sha256=sha(a.bridge),adapter_sha256=sha(a.adapter),rows=[],human_review='HUMAN_REVIEW_REQUIRED')
 for code in a.codes.split(','):
  r=next(r for r in m if r['game_code']==code);state=next(r['state'] for r in f if r['game']==code)
  folder=a.output/code
  cmd=[sys.executable,str(Path(__file__).with_name('test_phase3_audible.py')),'--core',str(a.core.resolve()),'--bridge',str(a.bridge.resolve()),'--adapter',str(a.adapter.resolve()),'--rom',r['rom'],'--state',state,'--output',str(folder.resolve()),'--schedule','1,2,3,1,2,3','--segment-seconds',str(a.seconds),'--incremental-log','--no-pcm-capture',*(['--audible','--video','--compact-capture'] if a.audible_video else ['--muted'])]
  with (a.output/(code+'.log')).open('xb') as log:q=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=6*a.seconds+30)
  row=dict(game=code,returncode=q.returncode,command=cmd)
  if (folder/'results.json').exists():row['measurement']=measure(folder)
  results['rows'].append(row);(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf8')
  print(code,q.returncode,[(s['target_speed'],round(s.get('internal_speed_x',0),5),s['steady_fixed']) for s in row.get('measurement',{}).get('segments',[])],flush=True)
  assert q.returncode==0,'Retain failed take; do not relabel it PASS'
if __name__=='__main__':main()
