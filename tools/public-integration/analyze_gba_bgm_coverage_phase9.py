"""Unretimed scene comparisons for the bounded Phase9 BGM survey.
SPDX-License-Identifier: MPL-2.0. No golden changes, playback or audio exports.
"""
import argparse
import json
import re
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

from analyze_phase2_scene import windows
from test_three_x_poc import sha


def read(p):
    return json.loads(p.read_text(encoding='utf8'))


def pcm(p):
    return np.fromfile(p,dtype='<i2').reshape(-1,2).mean(axis=1)/32768


def agreement(rows):
    return len(rows)==3 and min(x['correlation'] for x in rows)>.97


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--roots',nargs='+',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();attempts=[];identities={}
    for root in a.roots:
        manifest=read(root/'manifest.json');fixtures=read(root/'fixtures/results.json')['rows']
        for item in read(root/'audio/results.json')['rows']:
            code=item['game'];identity=next(r for r in manifest['rows'] if r['game_code']==code)
            identities[code]={k:identity.get(k) for k in ('title','release_id','region','revision','game_code','driver_family','sha256','sha1','crc32','rom_size','header_revision_direct','dat_rom_name')}
            folder=root/'audio'/code;raw={s:pcm(folder/'fixed'/str(s)/'callback.s16le') for s in (1,2,3)}
            native=pcm(folder/'native/callback.s16le');native_rates=item['native_reference']['av_rates'];native_rate=int(native_rates[-1][0]) if native_rates else 65536
            assert native_rate in (32768,65536)
            if native_rate==65536:native=resample_poly(native,1,2)
            extra=[]
            for speed in (2,3):
                rows=windows(raw[1],raw[speed],32768,starts=(3,6,9))
                extra.append(dict(speed=speed,windows=rows,agreement=agreement(rows),
                    phrase_time_slope=float(np.polyfit([r['fast_start_seconds'] for r in rows],[r['reference_start_seconds'] for r in rows],1)[0]) if len(rows)>1 else None))
            first=item['analysis']['comparisons']
            original=all(agreement(x['windows']) for x in first)
            steady=all(x['agreement'] for x in extra)
            logs={s:(folder/'fixed'/str(s)/'run.log').read_text(errors='replace') for s in (1,2,3)}
            routes=[]
            for speed,t in logs.items():
                active=re.findall(r'candidate_active=(\d)',t)
                routes.append(dict(speed=speed,final_fixed=bool(active) and active[-1]=='1',
                    fallback=item['scene']['records'][speed-1]['fallback'],
                    underrun=item['scene']['records'][speed-1]['underrun'],overrun=item['scene']['records'][speed-1]['overrun'],
                    queue_dropped_max=max([0]+list(map(int,re.findall(r'queue_dropped=(\d+)',t)))),
                    recovered_players=[l for l in t.splitlines() if 'STATE_LOAD_PLAYER_DISCOVERY ' in l],
                    play_stop_events=[l for l in t.splitlines() if '[MP2K SEMANTIC]' in l],
                    log_sha256=sha(folder/'fixed'/str(speed)/'run.log')))
            audible=float(np.sqrt(np.mean(raw[1]**2)))>1e-6
            native_audible=float(np.sqrt(np.mean(native**2)))>1e-6
            confirmed=(original or steady) and audible and native_audible and all(r['final_fixed'] and not r['underrun'] and not r['overrun'] and not r['queue_dropped_max'] and not r['fallback'] for r in routes)
            status='BGM_CONFIRMED' if confirmed else 'BGM_NOT_ACTIVE' if not audible or not all(r['final_fixed'] for r in routes) else 'BGM_INCONCLUSIVE'
            attempts.append(dict(game=code,root=str(root),fixture=next(r for r in fixtures if r['game']==code),
                status=status,scope='Tested loaded scene only; Fixed1x versus Fixed2x/3x normal sample scale. Native1x fidelity and listening are separately reported, not certified by this status.',
                confirmation_window_set='original 2/5/8s' if confirmed and original else 'universal steady 3/6/9s' if confirmed else None,
                original_comparisons=first,universal_steady_comparisons=extra,routes=routes,
                fixed1x_rms=float(np.sqrt(np.mean(raw[1]**2))),native1x_rms=float(np.sqrt(np.mean(native**2))),
                native1x_reference=dict(native_sample_rate=native_rate,conversion='Rate conversion only; no tempo/pitch scaling',
                    unretimed_windows=windows(native,raw[1],32768,starts=(3,6,9)),
                    interpretation='Phase/timbre/instrument mixtures differ between native GBA synthesis and bridge. These values are not a full native fidelity PASS; dominant peaks are mixture peaks, not isolated instrument pitch.',
                    human_listening='HUMAN_REVIEW_REQUIRED'),
                audio_hashes={str(s):sha(folder/'fixed'/str(s)/'callback.s16le') for s in (1,2,3)},
                native_audio_sha256=sha(folder/'native/callback.s16le'),
                same_initial_ram=len({r['initial_ram_sha256'] for r in item['scene']['records']})==1,
                se='Separate fidelity/lifetime not certified. Existing finite SFX shortening remains; never used as sole BGM failure criterion.'))
    selected=[]
    for code,identity in identities.items():
        candidates=[r for r in attempts if r['game']==code]
        good=[r for r in candidates if r['status']=='BGM_CONFIRMED']
        chosen=good[-1] if good else candidates[-1]
        selected.append(dict(identity=identity,result=chosen,attempt_count=len(candidates)))
    result=dict(method='Same exact ROM and native state per attempt; equal ideal audio duration, three unchanged 2s windows with +/-0.5s search and correlation >0.97. Original 2/5/8s retained; additional predefined 3/6/9s applied to EVERY attempt, never per-title-selected. No time stretching, pitch correction or golden update. 12s captures, no-input post-load. Unthrottled tests are not actual frontend speed proof.',
        counts={s:sum(r['result']['status']==s for r in selected) for s in ('BGM_CONFIRMED','BGM_INCORRECT','BGM_NOT_ACTIVE','BGM_INCONCLUSIVE')},
        rows=selected,attempts=attempts,human_listening='HUMAN_REVIEW_REQUIRED',all_native_fidelity_certified=False)
    with a.output.open('x',encoding='utf8') as out:out.write(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(result['counts'])


if __name__=='__main__':main()
