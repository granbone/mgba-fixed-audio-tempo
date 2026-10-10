"""Strict Phase6 lifetime checks against protected Phase5 evidence, no golden edits.
SPDX-License-Identifier: MPL-2.0. Private PCM is never copied into committed output.
"""
import argparse, json, re
from pathlib import Path
import numpy as np
from test_three_x_poc import sha

def read(p): return json.loads(p.read_text(encoding='utf8'))
def fields(s): return dict(re.findall(r'(\w+)=([^\s]+)', s))
def notes(r):
    return [{k:fields(s)[k] for k in ('sample','tick','player','track','key','priority','ch')}
            for s in r['voices'] if 'decision=REQUEST ' in s and 'player=2 track=0 ' in s]
def ends(r):
    return [fields(s) for s in r['voices'] if 'decision=END player=2 track=0 ' in s]
def first_sample(a,b):
    x=np.fromfile(a,dtype='<i2').reshape(-1,2);y=np.fromfile(b,dtype='<i2').reshape(-1,2)
    n=min(len(x),len(y));where=np.flatnonzero(np.any(x[:n]!=y[:n],axis=1))
    return int(where[0]) if len(where) else (n if len(x)!=len(y) else None)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('phase5','probe','production','output'): p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();old=read(a.phase5/'results.json');probe=read(a.probe/'results.json');prod=read(a.production/'results.json')
    reference=probe['runs'][0];assert len(notes(reference))==7 and len(ends(reference))==7
    rows=[]
    for speed in (1,2,3):
        b=old['runs'][speed-1];r=probe['runs'][speed-1];d=prod['runs'][speed-1]
        assert r['callback_sha256']==d['callback_sha256'] and r['event_sha256']==d['event_sha256']
        assert notes(r)==notes(reference) and ends(r)==ends(reference)
        native=[fields(s) for s in r['evidence'] if 'kind=NATIVE_FINISH ' in s and 'player=2 ' in s]
        explicit=[s for s in r['evidence'] if 'kind=NATIVE_STOP_CALL ' in s and 'player=2 ' in s]
        assert len(native)==1 and native[0]['clock']=='16' and not explicit
        exact=b['callback_sha256']==r['callback_sha256']
        assert exact == (speed!=2), 'Unexpected change outside corrected 2x fixture'
        play=next(fields(s) for s in r['evidence'] if 'type=PLAY_SONG song=202 ' in s)
        old_stop=[fields(s) for s in b['evidence'] if 'type=STOP_SONG song=202 ' in s]
        first=first_sample(a.phase5/f'experimental-{speed}/callback.s16le',a.production/f'experimental-{speed}/callback.s16le')
        log=(a.production/f'experimental-{speed}/run.log').read_text(errors='replace')
        callback=None;clock=None;run=None
        if first is not None:
            callback=next(fields(s) for s in log.splitlines() if '[HOST CALLBACK]' in s and
                int(fields(s)['byteOffset'])//4<=first<int(fields(s)['byteOffset'])//4+int(fields(s)['samples']))
            number=int(callback['frame'])+1
            clock=next((fields(s) for s in log.splitlines() if '[AUDIO CLOCK]' in s and fields(s).get('run')==str(number)),None)
            run=next((fields(s) for s in log.splitlines() if '[FIXED AUDIO RUN]' in s and fields(s)['run']==str(number)),None)
        rows.append(dict(speed=speed,play_sample=int(play['audioSample']),phase5_polled_stop=old_stop,
            native_finish=native,explicit_stop_calls=len(explicit),requests_before=notes(b),requests_after=notes(r),
            ends_before=ends(b),ends_after=ends(r),requests_exact_1x=True,ends_exact_1x=True,
            phase5_pcm_exact=exact,phase5_event_exact=b['event_sha256']==r['event_sha256'],
            first_pcm_difference_sample=first,first_pcm_difference_seconds=None if first is None else first/32768,
            first_difference_callback=callback,first_difference_clock_advance=clock,first_difference_generation_consumption=run,
            play_to_final_voice_end_samples=int(ends(r)[-1]['sample'])-int(play['audioSample']),
            probe_matches_production=True,status='PASS_MEASURED_SE202'))
    result=dict(inputs={str(p):sha(p) for p in (a.phase5/'results.json',a.probe/'results.json',a.production/'results.json')},
        rows=rows,cause='The Phase5 identity/FINE proof deliberately admitted only 3x. At 2x native completion was still translated into a hard bridge STOP.',
        scope='Shared supported 2x/3x handling within the existing verified AORJ SE202 route; no additional game/song exceptions or unchecked driver expansion.',
        pitch='Seven request/voice-end records exactly match 1x. Isolated synthesis PCM comparison separately tests pitch and duration without retiming.',
        timestamps='Callback file sample index includes native startup and PCM buffering; bridge note timestamps and independent-clock event timestamps are separate coordinates.',
        human_review_required=['Audible timbre and cutoff','Battle, loop and simultaneous SFX','Pitch bend','Perceptual Load/Rewind continuity'])
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps([dict(speed=r['speed'],status=r['status'],first_pcm_difference_sample=r['first_pcm_difference_sample']) for r in rows]))
if __name__=='__main__': main()
