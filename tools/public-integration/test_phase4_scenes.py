"""Rerun original-package/common-new-bridge loaded scenes plus 3x BGM.
SPDX-License-Identifier: MPL-2.0. Private PCM stays outside committed evidence.
"""
import argparse, json, subprocess, sys
from pathlib import Path
from test_three_x_poc import sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('core','bridge','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--root',type=Path,default=Path.cwd())
    p.add_argument('--runner',type=Path,default=Path('build-phase2/retro-runner.exe'))
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);root=a.root.resolve()
    public=root.parent/'mgba_public_v03_fresh/build-publication-distribution/package'
    oldcore=public/'cores/mgba_fixed_audio_libretro.dll';oldbridge=public/'runtime/libmgba_mp2k_bridge.dll'
    scenes=json.loads((root/'build-phase3/scenes.json').read_text());rows=[];measures={}
    for code,scene in scenes.items():
        for label,core,bridge,speeds in [('original',oldcore,oldbridge,'1,2'),('common',oldcore,a.bridge,'1,2'),('phase4',a.core,a.bridge,'1,2,3')]:
            folder=a.output/f'{label}-{code}'
            with (a.output/f'{label}-{code}.log').open('wb') as log:
                result=subprocess.run([sys.executable,str(Path(__file__).with_name('test_phase2_scene.py')),
                    '--runner',str(a.runner),'--core',str(core),'--bridge',str(bridge),
                    '--rom',scene['rom'],'--state',scene['state'],'--output',str(folder),
                    '--seconds','24','--speeds',speeds],stdout=log,stderr=subprocess.STDOUT,timeout=300)
            assert result.returncode==0,folder
        for speed in (1,2):
            ref=json.loads((a.output/f'phase4-{code}/results.json').read_text())['records'][speed-1]
            for label in ('original','common'):
                old=json.loads((a.output/f'{label}-{code}/results.json').read_text())['records'][speed-1]
                row=dict(game=code,speed=speed,comparison=label,pcm_exact=old['callback_sha256']==ref['callback_sha256'],
                    event_exact=old['events']==ref['events'],initial_ram_exact=old['initial_ram_sha256']==ref['initial_ram_sha256'],
                    reference_pcm_sha256=old['callback_sha256'],candidate_pcm_sha256=ref['callback_sha256'])
                rows.append(row);print(json.dumps(row),flush=True)
        measurement=a.output/f'{code}-bgm.json'
        with (a.output/f'{code}-bgm.log').open('wb') as log:
            result=subprocess.run([sys.executable,str(Path(__file__).with_name('analyze_phase2_scene.py')),
                str(a.output/f'phase4-{code}'),'--rate','65536' if code=='B6JJ' else '32768','--output',str(measurement)],stdout=log,stderr=subprocess.STDOUT,timeout=120)
        assert result.returncode==0
        measures[code]=json.loads(measurement.read_text())
        (a.output/'results.json').write_text(json.dumps(dict(comparisons=rows,bgm=measures,
            core_sha256=sha(a.core),bridge_sha256=sha(a.bridge),original_core_sha256=sha(oldcore),original_bridge_sha256=sha(oldbridge)),indent=2)+'\n')
    assert all(r['pcm_exact'] and r['event_exact'] and r['initial_ram_exact'] for r in rows)


if __name__=='__main__':main()
