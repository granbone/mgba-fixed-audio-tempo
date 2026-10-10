"""All-channel voice IDs and STOP source evidence, without guessing end intent.
SPDX-License-Identifier: MPL-2.0. Existing product policy is unchanged.
"""
import argparse,json,re
from pathlib import Path
from test_three_x_poc import sha

def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=a.root.resolve();assert not a.output.exists()
    reports=[r/'build-phase8/cross-title/results.json',r/'build-phase8/cross-title-A8CJ/results.json'];rows=[]
    for report in reports:
        data=json.loads(report.read_text())
        for title in data['rows']:
            code=title['game'];case=title['case'];speeds=[]
            for speed in (1,2,3):
                obs=next(x for x in title['observations'] if x['speed']==speed and x['instrumented_bridge'])
                play=next(x for x in obs['events'] if x['type']=='PLAY_SONG');stop=next(x for x in obs['events'] if x['type']=='STOP_SONG');start=int(play['audioSample']);finish=int(stop['audioSample'])
                folder=report.parent/f'{code}-experimental-{speed}-probe';text=(folder/'run.log').read_text(errors='replace')
                next_start=[]
                for line in text.splitlines():
                    if '[MP2K SEMANTIC]' in line and 'type=PLAY_SONG ' in line and f'player={case["player"]} ' in line:
                        f=fields(line)
                        if int(f['audioSample'])>start:next_start.append(int(f['audioSample']))
                boundary=min(next_start,default=1<<63)
                trace=[fields(x) for x in (folder/'voices.log').read_text().splitlines() if f'player={case["player"]} ' in x]
                requests=[x for x in trace if x['kind']=='NOTE_REQUEST' and start<=int(x['sample'])<boundary]
                born=[x for x in trace if x['kind']=='VOICE_BEGIN' and start<=int(x['sample'])<boundary]
                deaths={x['id']:x for x in trace if x['kind']=='VOICE_END'}
                voices=[]
                for x in born:
                    y=deaths.get(x['id']);assert y is not None
                    voices.append(dict(id=int(x['id']),player=int(x['player']),track=int(x['track']),key=int(x['key']),priority=int(x['priority']),
                        length_ticks=int(x['length']),start_sample=int(x['sample']),end_sample=int(y['sample']),duration_samples=int(y['sample'])-int(x['sample']),
                        final_synthesis_frequency=float(y['freq']),end_reason='UNKNOWN',env_state=int(y['state']),release_flag=int(y['stop'])))
                assert voices and len({x['id'] for x in voices})==len(voices)
                native=[x for x in obs['native'] if x['kind']=='NATIVE_FINISH' and int(play['cycle'])<int(x['cycle'])<=int(stop['cycle'])]
                explicit=[x for x in obs['native'] if x['kind']=='NATIVE_STOP_CALL' and int(play['cycle'])<int(x['cycle'])<=int(stop['cycle'])]
                assert len(native)==1 and not explicit
                note_selection=[(int(x['track']),int(x['key']),int(x['length'])) for x in requests]
                speeds.append(dict(speed=speed,semantic_play_requests=1,note_requests=len(requests),voices_selected=len(voices),
                    selected_note_sequence=note_selection,voices=voices,play_sample=start,poll_stop_sample=finish,poll_stop_sequence=int(stop['seq']),
                    native_finish=dict(source='NATURAL_END',instruction_pc=case['finish'],cycle=int(native[0]['cycle']),clock=int(native[0]['clock']),header=native[0]['header']),
                    polling_stop_reason='UNKNOWN_CANCEL_COVERAGE; observed native FINE is not full cancellation provenance',
                    checked_explicit_stop_calls_in_epoch=0,last_voice_end_ms=(max(x['end_sample'] for x in voices)-start)/32768*1000,
                    end_near_stop_count=sum(abs(x['end_sample']-finish)<=548 for x in voices),
                    voice_id_scope='Unique non-moving private bridge object lifetime; not a native voice generation or a core event ID.',
                    ring_underrun=obs['underrun'],ring_overrun=obs['overrun'],queue_drop=obs['queue_drop'],fallback=obs['fallback'],
                    trace_sha256=sha(folder/'voices.log'),callback_sha256=obs['callback_sha256']))
            reference=speeds[0]
            for item in speeds:
                item['last_voice_end_ratio_to_1x']=item['last_voice_end_ms']/reference['last_voice_end_ms']
                item['missing_note_requests_vs_1x']=reference['note_requests']-item['note_requests']
                n=min(item['voices_selected'],reference['voices_selected']);pairs=list(zip(reference['voices'][:n],item['voices'][:n]))
                item['common_prefix_track_keys_equal']=all((x['track'],x['key'])==(y['track'],y['key']) for x,y in pairs)
                item['common_prefix_final_frequency_equal_count']=sum(x['final_synthesis_frequency']==y['final_synthesis_frequency'] for x,y in pairs)
                item['frequency_comparison_limit']='End frequencies are synthesis parameters, not necessarily musical Hz; shortened notes can end at an earlier pitch-bend position. No whole-SE audible pitch PASS.'
            assert all(x['last_voice_end_ratio_to_1x']<.9 for x in speeds[1:])
            rows.append(dict(game=code,title=title['name'],song=case['song'],player=case['player'],driver=title['driver'],rom_sha256=title['rom_sha256'],
                fixture_sha256=title['state_sha256'],from_reset=title['from_reset'],speeds=speeds,
                production_probe_exact=title['production_probe_equivalence'],status='FAIL_LIFETIME_SHORTENING_REPRODUCED; PRODUCT_FIX_NOT_ADOPTED',
                lifetime_units='32768 Audio Clock samples/s. Bridge generated voice events may be lookahead, not heard output.',
                native_epoch_generation='UNKNOWN; registry generation is a rebind epoch, not proof of accepted same-header Start.',
                bgm_conflict='Not globally certified; finite target traced by player/header. Existing priority/voice selection unchanged.',
                untested=['loop SE cancellation','per-voice explicit cancellation','battle/pitch-bend listening','all custom or alternate SDK calls'],human_review='HUMAN_REVIEW_REQUIRED'))
    a.output.write_text(json.dumps(dict(rows=rows,new_production_fix=False,new_wavs=0,
        source_classification={'NATURAL_END':'checked native MPlayMain terminal instruction observed','EXPLICIT_STOP':'checked native Stop entry only',
            'VOICE_STEAL':'private PSG allocation decision only; not assigned to unidentified player polling STOP',
            'TRACK_END':'not classified from flags alone','SONG_CHANGE':'requires accepted Start return/owner replacement','STATE_RESTORE':'explicit Load/Rewind discard route',
            'UNKNOWN':'default for uncovered cancellation, unconfirmed epochs and individual voice-end causes'}),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print([(x['game'],[(s['speed'],s['note_requests'],s['voices_selected'],round(s['last_voice_end_ms'],3)) for s in x['speeds']]) for x in rows])

if __name__=='__main__':main()
