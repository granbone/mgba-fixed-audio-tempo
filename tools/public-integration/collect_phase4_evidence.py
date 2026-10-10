"""Collect bounded Phase4 results and private listening provenance, no playback.
SPDX-License-Identifier: MPL-2.0. Contains no ROM, state or audio in public output.
"""
import argparse, json, wave
from collections import Counter
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly
from test_three_x_poc import sha
from test_phase4_priority import fields


def read(p):return json.loads(p.read_text())
def write(p,obj):p.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


def selection_evidence(root):
    private=root/'build-phase4';rows=[]
    for speed in (1,2,3):
        folder=private/('aorj-comparison-full' if speed<3 else 'aorj-priority-three')
        models={}
        for label in ('B','D'):
            lines=(folder/f'{label}-instrumented-{speed}-0/psg.log').read_text().splitlines()
            allocations=[]
            for line in lines:
                f=fields(line)
                if f.get('decision')=='REQUEST':allocations.append(dict(request=f))
                elif f.get('decision') in ('ACCEPT','REJECT_HIGHER','REJECT_TIE'):
                    allocations[-1]['decision']=f['decision']
                elif f.get('decision') in ('STEAL','REPLACE_RELEASE'):
                    allocations[-1]['replacement']=f
            models[label]=allocations
        assert len(models['B'])==len(models['D'])
        changed=[]
        for before,after in zip(models['B'],models['D']):
            assert all(before['request'][k]==after['request'][k] for k in ('sample','tick','header','player','track','key','ch'))
            if before['decision']!=after['decision']:changed.append(dict(before=before,after=after))
        counts=Counter((c['after']['request']['player'],c['before']['decision'],c['after']['decision']) for c in changed)
        rows.append(dict(speed=speed,requests_equal_except_priority=True,request_count=len(models['B']),
            changed_selection_count=len(changed),changed_selections=changed[:8],
            changes_by_rule=[dict(player=k[0],before=k[1],after=k[2],count=v) for k,v in counts.items()],
            private_detail_logs={str(folder/f'{label}-instrumented-{speed}-0/psg.log'):sha(folder/f'{label}-instrumented-{speed}-0/psg.log') for label in ('B','D')}))
    native=[]
    for line in (private/'aorj-comparison-full/E-native-1-0/run.log').read_text().splitlines():
        if '[HOST NATIVE PSG]' not in line:continue
        f=fields(line)
        if f['track'] in ('03005a10','03005b00'):native.append(f)
    representatives=[]
    for pointer in ('03005a10','03005b00'):
        entries=[r for r in native if r['track']==pointer]
        representatives.extend(entries[:6])
    return dict(allocation_comparisons=rows,native_se_transitions=representatives,
        native_private_detail_log=str(private/'aorj-comparison-full/E-native-1-0/run.log'),
        interpretation='B/D instrumented source models are distinct DLLs; exact production/model PCM agreement verified separately. All native PSG transition hashes match across canonical A/B/C/D/E runs. Map native track03005a10 toplayer2track0 and03005b00 toplayer3track1 using verified ROM table. A/C allocation inference is identified by source and matching production PCM, not purported added instrumentation in immutable distributed DLLs.')


def write_summary(root):
    folder=root/'docs/three-x-phase4-evidence'
    common=read(folder/'regression-common48.json');compat=read(folder/'bridge-compatibility48.json')
    three=read(folder/'three36.json');scene=read(folder/'scenes16.json');front=read(folder/'frontend.json')
    se=read(folder/'se-stop.json');runtime=read(folder/'runtime.json')
    games=[]
    for code in ('AAMJ','AFXJ','AORJ','B6JJ'):
        live=front['muted_switch_recovery'][code]
        games.append(dict(game=code,pre_recovery_real_3x=[s['pre_recovery_speed_x'] for s in live['pre_recovery_spans'] if s['target_speed']==3],
            bgm_tempo='MEASURED_1X_IN_LOADED_SCENE',bgm_pitch='UNRETIMED_WAVEFORM_SPECTRUM_AGREEMENT_IN_LOADED_SCENE',
            bgm=scene['bgm'][code],se='CONFIRMED_INHERITED_STOP_TRUNCATION_AT_2X_3X; priority gap corrected' if code=='AORJ' else 'HUMAN_REVIEW_REQUIRED',
            switch='Headless12321/131/313 and actual muted1313 completed; perceptual continuity HUMAN_REVIEW_REQUIRED',
            load_rewind='Known ownership HARD fallback maintained for gameplay fixture; actual BGM fixture recovered' if code=='AAMJ' else
                'Continuation recovered; NCI produced2 rewind episodes, each finalRebuilds1/intermediate0. Existing continuous headless suite PASS' if code=='B6JJ' else
                'Recovered in actual frontend and headless matrix; stale sample/perceptual continuity HUMAN_REVIEW_REQUIRED',
            new_physical_windows_capture=code=='AORJ',human_review='HUMAN_REVIEW_REQUIRED'))
    fallback=[dict(case=r['label'],reasons=r['safety_fallback_reasons']) for r in three['runs'] if r['safety_fallback_reasons']]
    report=dict(schema_version=1,date='2026-10-10',phase='v0.4 Phase4 MP2K PSG Priority & SFX Fidelity',
        git=dict(worktree=str(root),branch='feature/3x-fixed-audio-poc',start_head='d4ee75d34371fa2335514eb85cabd55a73294f23',
            source_head=runtime['source_head'],final_head='Commit containing this report; see git log -1 / final response.',
            public_baseline='91f153b4e7fd192872a892d81f37501b3a8d4304',push=False,tag_created=False,release_created=False),
        implementation=dict(adopted=True,rule='min(255, player.priority + track.priority), snapshotted at note creation',
            stable_tie_order_maintained=True,production_files=['tools/mp2k-audio-trace/bridge/priority_order.hpp','tools/mp2k-audio-trace/bridge/CMakeLists.txt'],
            clock_event_b6jj_core_source_changed=False,native_reference_cases=65536,native_tie_cases=324,
            note_request_series_unchanged=True,maximum_existing_psg_voices_per_lane=1,
            changed_allocation_counts=[r['changed_selection_count'] for r in read(folder/'voice-selection.json')['allocation_comparisons']]),
        runtime=runtime,games=games,
        regressions=dict(common_phase4_bridge_48=dict(exact=48,total=48),old_bridge_compatibility=dict(pcm_event_exact=compat['exact'],event_exact=compat['event_exact'],total=48,
                intentional_changes=[r for r in compat['comparisons'] if not r['pcm_exact']]),
            original_package_loaded_scene=dict(exact=8,total=8),public_core_common_new_bridge_loaded_scene=dict(exact=8,total=8),
            legacy12=read(folder/'legacy12.json'),clean_committed_build_scene_comparisons=dict(exact=12,total=12),
            three_matrix_runs=len(three['runs']),ewram_safe30='Protected inherited evidence; representativeAAKJ/A2QJ and hard guards/recovery rerun, not all30 re-certified',
            disabled='Strict unchanged PCM/event in all8 canonical Disabled2x cases; legacy off1x/2x coverage and3x native output retained'),
        se=dict(priority_missing_selection='CORRECTED_IN_TESTED_AORJ_CONFLICTS_AT_1X_2X_3X',stop_problem=se,
            isolated_pitch_and_perceptual_completeness='HUMAN_REVIEW_REQUIRED',blind_ignore_stop_not_adopted=True,
            fallback_consideration='Condition-limited unsupported STOP handling can be considered after identifying explicit cancellation vs native-finished cleanup. Blanket3x native fallback loses1x BGM and does not fix normal SFX lifetime.'),
        safety=dict(guards_unchanged=True,legacy_hard_guard_suite_pass=True,
            ring_underrun_max=max(r['underrun'] for r in three['runs']),ring_overrun_max=max(r['overrun'] for r in three['runs']),
            queue_peak=max(r['queue_peak'] for r in three['runs']),queue_drop_max=max(r['queue_dropped'] for r in three['runs']),
            semantic_queue_capacity=512,expected_or_known_fallback_cases=fallback,
            physical_device_underruns='NOT_INSTRUMENTED',mp2k_stale_pcm_in_frontend='NOT_DIRECTLY_INSTRUMENTED',b6jj_stale_queue_pcm_markers=[0,0]),
        windows=dict(aorj_actual_real_speeds={k:v['segments'][0]['measured_speed_x'] for k,v in front['aorj_windows'].items()},
            aorj_fixed_samples_per_second={k:v['segments'][0]['fixed_samples_per_second'] for k,v in front['aorj_windows'].items() if k!='native'},
            one_core_cpu_percent={k:v['one_core_cpu_percent'] for k,v in front['aorj_windows'].items()},
            capture='Actual XAudio2 -> WASAPI192kHz endpoint mix; stored48kHz stereo. No time/pitch correction; not process-isolated.',
            no_visible_video=True,system_volume_changed=False,foreign_emulator_process_modified=False,
            other_games_new_actual_output='Muted backend/frontend tests and protected Phase3 endpoint evidence only; no new physical hearing claims',
            native_rejected_trial_retained=True,owned_audible_processes_exited=True),
        human_review=dict(agent_listening_performed=False,new_human_listening_pass=False,
            inherited_user_quote='何も違和感がない音楽にきこえます',inherited_scope='Limited Phase3 B6JJ BGM observation; not extended to Phase4 SFX',
            remaining=['AORJ changed SFX timbre/pitch','Battle SFX','Concurrent/rapid SFX','Pitch bend','Perceptual Load/Rewind/switch continuation','Endpoint mix contamination'],
            private_materials=11,manifest='build-phase4/listening-review/manifest.json'),
        retention=read(folder/'protection.json'),
        release_decision='HOLD_FOR_INHERITED_SE_STOP_AND_HUMAN_REVIEW',
        next_work=['Classify AORJ valid STOP callers/guarded player finished states without changing explicit cancellation semantics',
            'Review11 short private clips and select limited battle/concurrent scenes','Process-isolated Windows capture','Single continuous frontend rewind verification','Broader EWRAM safe30 coverage'],
        evidence={p.name:dict(path=p.relative_to(root).as_posix(),sha256=sha(p)) for p in sorted(folder.glob('*.json'))})
    write(root/'docs/THREE_X_PHASE4.json',report)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path.cwd())
    a=p.parse_args();root=a.root.resolve();private=root/'build-phase4'
    evidence=root/'docs/three-x-phase4-evidence';evidence.mkdir(exist_ok=False)
    common=[read(private/n/'results.json') for n in ('regression-main','regression-rest')]
    common48=dict(checks=[r for x in common for r in x['checks']],runs=[r for x in common for r in x['runs']],
        method='Public v0.3 core and Phase4 core BOTH use Phase4 bridge. PCM/event exactness is strict. Separately assess bridge compatibility below.')
    assert len(common48['checks'])==48 and all(r['byte_exact'] for r in common48['checks'])
    old={r['label']:r for r in read(root/'docs/three-x-phase2-evidence/regression-48.json')['runs']}
    compatibility=[]
    for r in common48['runs']:
        if not r['label'].endswith('-poc'):continue
        before=old[r['label']];pcm=r['callback_sha256']==before['callback_sha256'];event=r['event_sha256']==before['event_sha256']
        row=dict(case=r['label'],pcm_exact=pcm,event_exact=event,old_pcm_sha256=before['callback_sha256'],new_pcm_sha256=r['callback_sha256'])
        if not pcm:
            assert r['label'] in ('AORJ-1x-poc','AORJ-2x-poc','AORJ-switch-poc') and event
            prior=root/'build-phase2/regression-main'/r['label']/'callback.s16le'
            current=private/'regression-main'/r['label']/'callback.s16le'
            x,y=[np.fromfile(f,'<i2').reshape(-1,2) for f in (prior,current)];n=min(len(x),len(y))
            different=np.flatnonzero(np.any(x[:n]!=y[:n],axis=1));first=int(different[0])
            row.update(first_difference_sample=first,seconds=first/32768,
                assessment='INTENTIONAL_NATIVE_PRIORITY_CORRECTION; not a byte-exact pass')
        compatibility.append(row)
    write(evidence/'regression-common48.json',common48)
    write(evidence/'bridge-compatibility48.json',dict(comparisons=compatibility,
        exact=sum(r['pcm_exact'] and r['event_exact'] for r in compatibility),event_exact=sum(r['event_exact'] for r in compatibility),
        method='Fresh Phase4 runs against protected Phase2 old-bridge baselines; no baseline WAV or tolerance changed. Public original A/B vs D is separately repeated in priority-comparison.json.'))
    for name,source in [('native-rule','native-rule-retry'),('priority-comparison','aorj-comparison-full'),
                        ('legacy12','legacy-regression'),('scenes16','scenes'),('three36','three'),
                        ('priority-three','aorj-priority-three')]:
        write(evidence/(name+'.json'),read(private/source/'results.json'))
    write(evidence/'voice-selection.json',selection_evidence(root))
    post=[]
    for code in ('AAMJ','AFXJ','AORJ','B6JJ'):
        before=read(private/f'scenes/phase4-{code}/results.json')['records']
        after=read(private/f'committed-scene-{code}/results.json')['records']
        for b,c in zip(before,after):
            row=dict(game=code,speed=b['speed'],pcm_exact=b['callback_sha256']==c['callback_sha256'],events_exact=b['events']==c['events'],initial_ram_exact=b['initial_ram_sha256']==c['initial_ram_sha256'])
            assert all(row[k] for k in ('pcm_exact','events_exact','initial_ram_exact'));post.append(row)
    write(evidence/'committed-build12.json',post)
    se=[]
    for speed in (1,2,3):
        folder=private/f'aorj-one-SE-probe/{speed}'
        notes=[fields(x) for x in (folder/'psg.log').read_text().splitlines() if 'player=2 track=0' in x]
        requests=[x for x in notes if x.get('decision')=='REQUEST']
        ends=[x for x in notes if x.get('decision')=='END']
        meta=read(private/'aorj-one-SE/results.json')['records'][speed-1]
        phase3=read(private/'aorj-one-SE-phase3/results.json')['records'][speed-1]
        events=[fields(x) for x in meta['events'] if 'song=202 player=2' in x]
        start=next(int(x['audioSample']) for x in events if x['type']=='PLAY_SONG')
        stop=next(int(x['audioSample']) for x in events if x['type']=='STOP_SONG')
        se.append(dict(speed=speed,request_keys=[int(x['key']) for x in requests],
            requests=requests,ends=ends,play_sample=start,stop_sample=stop,game_controlled_play_to_stop_ms=(stop-start)/32768*1000,
            pcm_matches_phase3=meta['callback_sha256']==phase3['callback_sha256'],events_match_phase3=meta['events']==phase3['events'],
            production_matches_instrumented=meta['callback_sha256']==read(private/'aorj-one-SE-probe/results.json')['records'][speed-1]['callback_sha256']))
    write(evidence/'se-stop.json',dict(scene='Isolated native public-core state saved after boot frame1000; A at19*speed game frames, song202 player2; same audio-time onset.',
        records=se,assessment='CONFIRMED_INHERITED_ACCELERATED_STOP_TRUNCATION_AT_2X_AND_3X',
        explanation='Early request spacing remains548 samples (1x sequencer stepping); accelerated valid MPLAY_STOP stops the independent player before later notes. Ignoring STOP cannot distinguish explicit cancellation from cleanup and was not adopted. No source/safety changes to event timing.'))
    live={};muted={}
    for code in ('AAMJ','AFXJ','AORJ','B6JJ'):
        row=read(private/f'live-muted-{code}/results.json')
        clean=[]
        # Segment1 includes LOAD at+8s; derive a genuinely steady pre-load span.
        for index,seg in enumerate(row['segments']):
            end=min(seg['end']-1,seg['begin']+(7.5 if index==1 else 4.5 if index==3 else 9))
            q=[o for o in seg['probes'] if seg['begin']+2<o[0]<end and o[4]]
            speed=None;sample_rate=None
            if len(q)>1:
                x,y=q[0],q[-1];dt=(y[2]-x[2])/1e6
                speed=(y[1]-x[1])/dt/59.727501;sample_rate=(y[3]-x[3])/dt
            clean.append(dict(segment=index,target_speed=seg['target_speed'],pre_recovery_speed_x=speed,pre_recovery_audio_samples_per_second=sample_rate,
                all_steady_probes_fixed=bool(q) and all(o[4] for o in q),scope='Exclude explicit load/rewind/toggle; no physical output capture in muted runs'))
        row['pre_recovery_spans']=clean;muted[code]=row
    for speed in (1,2,3):live[str(speed)]=read(private/f'live-AORJ-one-SE-{speed}/results.json')
    live['native']=read(private/'live-AORJ-one-SE-native-retry/results.json')
    write(evidence/'frontend.json',dict(muted_switch_recovery=muted,aorj_windows=live,
        rejected_native_first_capture=read(private/'live-AORJ-one-SE-native/results.json'),
        rejection='First native attempt had no SEGMENT_BEGIN due to fixed-only load detector; no controlled SE. Retained, not counted. Detector fixed for Disabled; retry confirms load and sends A.',
        audio_policy='Null video, default background muted; only AORJ output used brief audible capture. All owned processes exited. No global volume or other emulator modified.',
        endpoint_mix_warning='WASAPI endpoint mix, not process-isolated. An unrelated RetroArch/FCEUmm process was observed after testing; untouched. Device contamination cannot be excluded. Listening HUMAN_REVIEW_REQUIRED.'))
    review=private/'listening-review';review.mkdir(exist_ok=False);manifest=[]
    def save_pcm(name,path,rate,begin,end,core,bridge,description):
        samples=np.fromfile(path,'<i2').reshape(-1,2)[int(begin*rate):int(end*rate)]
        save_wave(name,samples,rate,dict(source=str(path),source_sha256=sha(path),source_begin=begin,source_end=end,
            core_sha256=core,bridge_sha256=bridge,scene=description))
    def save_wave(name,samples,rate,meta):
        # Format conversion only; no time/pitch correction or normalization.
        if rate!=48000:
            from math import gcd
            g=gcd(rate,48000);samples=np.rint(np.clip(resample_poly(samples.astype(float),48000//g,rate//g,axis=0),-32768,32767)).astype('<i2')
        path=review/name
        with wave.open(str(path),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(samples.tobytes())
        manifest.append(dict(file=str(path),sha256=sha(path),seconds=len(samples)/48000,rate=48000,game='AORJ',**meta,human_review='HUMAN_REVIEW_REQUIRED'))
    matrix=read(private/'aorj-comparison-full/results.json')['runs']
    for index,label in enumerate(('A-original','B-phase3','C-phase3-probe','D-phase4'),1):
        row=next(r for r in matrix if r['case']==f'{label}-1-0')
        save_pcm(f'{index:02d}-{label}-1x-SE202.wav',private/f'aorj-comparison-full/{label}-1-0/callback.s16le',32768,16.9,17.8,
            row['core_sha256'],row['bridge_sha256'],'Canonical boot/menu A atframe1020: PSG1 SE202 priority50 vs BGM20; same scene and sample range.')
        manifest[-1]['speed']=1
    for speed in (1,2,3):
        inp=read(private/'aorj-one-SE/results.json')['inputs']
        save_pcm(f'{4+speed:02d}-D-phase4-{speed}x-single-SE202.wav',private/f'aorj-one-SE/{speed}/callback.s16le',32768,.2,.95,
            sha(private/'candidate/runtime/mgba_fixed_audio_libretro.dll'),sha(private/'candidate/runtime/libmgba_mp2k_bridge.dll'),
            'Same isolated pre-SE fixture; A input at19*speed. Includes accelerated STOP; compare individual SE sequence length.');manifest[-1]['speed']=speed
    for index,(label,row) in enumerate(live.items(),8):
        source=private/(f'live-AORJ-one-SE-{label}' if label!='native' else 'live-AORJ-one-SE-native-retry')/'windows-loopback.wav'
        press=next(c['elapsed'] for c in row['commands'] if c['command']=='RETROPAD' and c['value']==1)
        with wave.open(str(source),'rb') as w:rate=w.getframerate();samples=np.frombuffer(w.readframes(w.getnframes()),'<i2').reshape(-1,2)
        # Core-input command and WASAPI input stream have different start offsets;
        # this broad window avoids claiming sample-exact endpoint event alignment.
        begin=max(0,press-.7);end=press+1.3
        save_wave(f'{index:02d}-Windows-{label}-SE202.wav',samples[int(begin*rate):int(end*rate)],rate,
            dict(source=str(source),source_sha256=sha(source),source_begin=begin,source_end=end,
                speed=int(label) if label!='native' else 1,mode=row['mode'],
                core_sha256=sha(private/'committed/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(private/'committed/runtime/libmgba_mp2k_bridge.dll'),
                scene='Windows endpoint mix around single A input; actual native owner/semantic logs identify song202. Broad alignment, device mix contamination not excluded.'))
    write(review/'manifest.json',manifest);write(evidence/'listening-manifest.json',manifest)
    (review/'README.md').write_text('Private Phase4 review: 11 short AORJ clips; originals retained.\n'
        '01..04 same canonical1x scene A/B/C/D, 05..07 identical loaded SE fixture at1/2/3x;08..11 physical endpoint captures.\n'
        'No automatic playback. No listening pass. See manifest.json for hashes/modes and source offsets.\n',encoding='utf8')
    originals=read(root/'docs/three-x-phase3-evidence/original-wavs.json')
    protected=read(root/'docs/three-x-phase3-evidence/protection.json')['phase3_protected_wavs']
    assert all(sha(Path(r['path']))==r['sha256'] for r in originals+protected)
    public=root.parent/'mgba_public_v03_fresh/build-publication-distribution/package'
    artifacts={str(f):sha(f) for f in (public/'cores/mgba_fixed_audio_libretro.dll',public/'runtime/libmgba_mp2k_bridge.dll',
        root.parent/'mgba_republication_audit_20261009/v03-publication/anonymous-downloads/mgba-fixed-audio-tempo-v0.3-preview-win64.zip')}
    write(evidence/'protection.json',dict(original_27_unchanged=True,phase3_25_wavs_unchanged=True,public_artifacts=artifacts,
        pinned_reader_sha256=sha(root.parent/'mgba_public_v03_fresh/build-publication-inputs/agbplay/src/agbplay/SequenceReader.cpp'),
        private_phase4_wavs_protected=[dict(path=str(f),sha256=sha(f)) for f in private.rglob('*.wav')],
        phase4_bytes=sum(f.stat().st_size for f in private.rglob('*') if f.is_file()),
        storage='Bounded required regression PCM/state/logs dominate. No old data deleted or moved. No MP4/long WAV proliferation;11 curated clips and5 bounded Windows source WAVs (one rejected trial).'))
    write(evidence/'runtime.json',dict(source_head='3461002e4b9fb6d991773d838e816a134875df09',
        committed_runtime={str(f):sha(f) for f in (private/'committed/runtime').glob('*.dll')},
        candidate_runtime={str(f):sha(f) for f in (private/'candidate/runtime').glob('*.dll')},
        candidate_build_basis='d4ee75d34371fa2335514eb85cabd55a73294f23 plus exact Phase4 production changes committed in3461002. Core audio source unchanged; clean committed rebuild verified by12 scene PCM/event comparisons.',
        probe_sha256=sha(private/'priority-probe-timing/build/libmgba_mp2k_bridge.dll'),
        runner_sha256=sha(private/'retro-runner.exe'),legacy_runner_sha256=sha(root/'build-phase2/retro-runner.exe')))
    write_summary(root)
    print('Collected',len(list(evidence.glob('*.json'))),'public metadata reports;',len(manifest),'private clips.')


if __name__=='__main__':main()
