"""Explain each strict regression difference using semantic STOP evidence.
SPDX-License-Identifier: MPL-2.0. No golden replacement or waveform tolerance.
"""
import argparse,json,re
from collections import Counter
from pathlib import Path
import numpy as np
from test_three_x_poc import sha,FPS

def read(p):return json.loads(p.read_text(encoding='utf8'))
def fields(s):return dict(re.findall(r'(\w+)=([^\s]+)',s))
def first(a,b):
    pos=0
    with a.open('rb') as x,b.open('rb') as y:
        while True:
            u=x.read(1048576);v=y.read(1048576)
            if not u or not v:return None if u==v else pos
            n=min(len(u),len(v))//4
            q=np.flatnonzero(np.any(np.frombuffer(u[:n*4],dtype='<i2').reshape(-1,2)!=np.frombuffer(v[:n*4],dtype='<i2').reshape(-1,2),axis=1))
            if len(q):return pos+int(q[0])
            pos+=n
            if len(u)!=len(v):return pos
def semantics(text):
    return Counter(tuple(fields(s)[k] for k in ('type','song','player','cycle','audioSample'))
                   for s in text.splitlines() if '[MP2K SEMANTIC]' in s)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path.cwd());a=p.parse_args();r=a.root.resolve();d=r/'build-phase6'
    matrix=read(d/'regression48/results.json');p5=read(r/'build-phase5/release-48/results.json');rows=[]
    for check in matrix['checks']:
        label=check['case']+'-poc';new=next(x for x in matrix['runs'] if x['label']==label);old=next(x for x in p5['runs'] if x['label']==label)
        oldfile=r/'build-phase5/release-48'/label/'run.log';newfile=d/'regression48'/label/'run.log'
        ot=oldfile.read_text(errors='replace');nt=newfile.read_text(errors='replace')
        pcm_exact=old['callback_sha256']==new['callback_sha256'];event_exact=old['event_sha256']==new['event_sha256']
        removed=semantics(ot)-semantics(nt);added=semantics(nt)-semantics(ot)
        intentional=not(pcm_exact and event_exact)
        clocks=lambda s:[x for x in s.splitlines() if '[AUDIO CLOCK]' in x]
        assert clocks(ot)==clocks(nt),label+' clock changed'
        proof_lines=[s for s in nt.splitlines() if '[MP2K NATURAL COMPLETE] player=2 song=202 nativeClock=16 ' in s]
        if intentional:
            assert check['case'] in ('AORJ-2x','AORJ-switch','AORJ-recovery'),check
            assert removed and not added,(label,removed,added)
            assert all(x[0:3]==('STOP_SONG','202','2') for x in removed)
            assert len(proof_lines)>=sum(removed.values())
            # Every removed command was a synthetic polling STOP at 2x,
            # never an explicit native command or a 1x notification.
            ratio=1.;matched=0
            for s in ot.splitlines():
                if '[MP2K FRONTEND]' in s:
                    f=fields(s);ratio=float(f['rate'])/FPS
                if '[MP2K SEMANTIC]' in s:
                    f=fields(s);key=tuple(f[k] for k in ('type','song','player','cycle','audioSample'))
                    if key in removed:
                        assert 1.99<=ratio<=2.01 and int(f['src'])>=(1<<63),(label,s,ratio)
                        matched+=1
            assert matched==sum(removed.values())
        else:assert check['byte_exact']
        sample=first(r/'build-phase5/release-48'/label/'callback.s16le',d/'regression48'/label/'callback.s16le') if intentional else None
        rows.append(dict(case=check['case'],public_common_bridge_byte_exact=check['byte_exact'],phase5_pcm_exact=pcm_exact,phase5_event_exact=event_exact,
            clock_progress_exact=True,callback_frame_count_exact=old['callback_samples']==new['callback_samples'],
            first_changed_callback_sample=sample,first_changed_callback_seconds=None if sample is None else sample/32768,
            removed_semantic_commands=[dict(type=x[0],song=int(x[1]),player=int(x[2]),cycle=int(x[3]),audio_sample=int(x[4]),count=n) for x,n in removed.items()],
            new_natural_completion_proofs=proof_lines,intentional_correction_verified=intentional,
            classification='EXPECTED_LIFETIME_CORRECTION' if intentional else 'BYTE_EXACT'))
    assert len(rows)==48 and all(x['callback_frame_count_exact'] for x in rows)
    result=dict(strict_exact_count=sum(x['public_common_bridge_byte_exact'] for x in rows),total=48,
        intentional_difference_count=sum(x['intentional_correction_verified'] for x in rows),unexplained_difference_count=0,
        rows=rows,goldens_updated=False,strict_mismatches_kept_in_runner=True,
        method='Strict PCM/event hashes retained; only deleted, 2x polled STOP202 commands with checked native-clock16 completion are justified. Other semantic commands and clock advancement must remain identical.')
    (d/'regression-analysis.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps({k:v for k,v in result.items() if k!='rows'}))
if __name__=='__main__':main()
