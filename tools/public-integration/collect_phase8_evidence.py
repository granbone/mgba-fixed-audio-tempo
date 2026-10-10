"""Collect Phase8 diagnostics without adopting a generalized STOP policy.
SPDX-License-Identifier: MPL-2.0. Media stays private; no publication or cleanup.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from analyze_phase7_frontend import analyze
from analyze_phase8_lifetimes import fields
from test_three_x_poc import sha

START = '9adcdd7b3f17d673628a2f5a48f3e17f46530630'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value, *, replace=False):
    with path.open('w' if replace else 'x', encoding='utf8') as out:
        out.write(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--refresh-metadata', action='store_true', help='Rebuild only Phase8 committed JSON metadata after verifying its previous hashes')
    args = parser.parse_args()
    root = Path.cwd()
    private = root / 'build-phase8'
    subprocess.run([sys.executable, str(root / 'tools/public-integration/collect_phase7_evidence.py'),
                    '--verify-only'], check=True)
    assert git(root, 'branch', '--show-current') == 'feature/3x-fixed-audio-poc'
    subprocess.run(['git', 'merge-base', '--is-ancestor', START, 'HEAD'], check=True)
    assert not git(root, 'diff', START, '--', 'src', 'include', 'tools/mp2k-audio-trace/bridge')
    listing = read(private / 'listening-review-final/manifest.json')
    assert len(listing['rows']) == 31 and listing['new_wavs'] == 12 and not listing['autoplay']
    for row in listing['rows']:
        assert sha(Path(row['path'])) == row['sha256']
    assert sum(row['new_recording'] for row in listing['rows']) == 12
    if args.verify_only or args.refresh_metadata:
        summary = read(root / 'docs/THREE_X_PHASE8.json')
        for evidence in summary['evidence']:
            assert sha(root / evidence['path']) == evidence['sha256']
        diagnostic = summary['runtime']['diagnostic_bridge']
        assert sha(Path(diagnostic['path'])) == diagnostic['sha256']
        assert sha(Path(diagnostic['generator'])) == diagnostic['generator_sha256']
        for item in diagnostic.get('copied_source_files', []):
            assert sha(Path(item['path'])) == item['sha256']
        if args.verify_only:
            print('Phase8 evidence, unchanged production source/runtime/public v0.3, 103 originals and 12 new clips verified')
            return

    out = root / 'docs/three-x-phase8-evidence'
    assert args.refresh_metadata or not out.exists()
    def emit(path, value):
        write(path, value, replace=args.refresh_metadata)
    # Validate every completed test before creating the committed metadata.
    reports = {name: read(private / file) for name, file in {
        'native': 'native-checked/results.json', 'lifetimes': 'lifetime-analysis.json',
        'model': 'lifetime-model-final.json', 'counterfactual': 'isolated-counterfactual.json',
        'regression48': 'regression48/results.json', 'scenes16': 'scenes16/results.json',
        'legacy12': 'legacy12/results.json', 'three72': 'three72/results.json',
        'guards28': 'guards28/results.json', 'recovery': 'replay/results.json',
        'latency': 'latency/results.json', 'additional-bgm': 'additional-bgm/results.json',
        'aorj': 'aorj-probe/results.json',
    }.items()}
    reg = reports['regression48']
    assert len(reg['rows']) == 48 and all(x['returncode'] == 0 and x['pcm_exact'] and x['event_exact'] for x in reg['rows'])
    scenes = reports['scenes16']
    assert len(scenes['comparisons']) == 16 and all(x['pcm_exact'] and x['event_exact'] and x['initial_ram_exact'] for x in scenes['comparisons'])
    assert reports['legacy12']['passed'] and len(reports['legacy12']['cases']) == 12
    three = reports['three72']['runs']
    before = {x['label']: x for x in read(root / 'docs/three-x-phase6-evidence/three72.json')['runs']}
    assert len(three) == 72 and len(before) == 72
    exact72 = []
    for run in three:
        old = before[run['label']]
        assert run['returncode'] == 0
        assert run['callback_sha256'] == old['callback_sha256'] and run['event_sha256'] == old['event_sha256']
        assert sha(private / 'three72' / run['label'] / 'callback.s16le') == run['callback_sha256']
        exact72.append(dict(case=run['label'],pcm_exact=True,event_exact=True))
    assert all(x['underrun'] == x['overrun'] == x['queue_dropped'] == 0 for x in three)
    guard_checks = []
    guards = reports['guards28']['runs']
    assert len(guards) == 28 and all(x['returncode'] == 0 for x in guards)
    for run in guards:
        label, reasons, applies = run['label'], run['safety_fallback_reasons'], True
        if 'explicit-disabled-conflict' in label:
            assert not run['fixed_activation'] and not run['final_fixed']
        elif run['game_code'] == 'B6JJ' and ('ring-overflow' in label or 'ring-shortage' in label):
            applies = False
        elif '-rate-' in label:
            reason = 'UNSUPPORTED_FRONTEND_CLOCK' if run['game_code'] == 'B6JJ' else 'FRONTEND_THROTTLE_TEMPORARILY_UNSUPPORTED'
            assert any(reason in x for x in reasons)
        elif '-partial' in label:
            assert any('FRONTEND_PARTIAL_AUDIO_BATCH' in x for x in reasons)
        elif 'ring-overflow' in label:
            assert run['overrun'] == 1 and any('RING_OVERRUN' in x for x in reasons)
        elif 'ring-shortage' in label:
            assert run['underrun'] == 1 and any('RING_UNDERRUN_OR_PENDING_FIRST_TICK' in x for x in reasons)
        else:
            raise AssertionError(label)
        guard_checks.append(dict(case=label,applicable=applies,status='PASS_EXPECTED_GUARD' if applies else 'NOT_APPLICABLE',reasons=reasons))
    assert sum(x['applicable'] for x in guard_checks) == 26
    native = reports['native']
    assert sha(private / 'native-stop-provenance-checked.exe') == native['executable_sha256']
    assert sha(Path(native['source'])) == native['source_sha256']
    assert len(native['rows']) == 4 and all(x['returncode'] == 0 for x in native['rows'])
    for row in native['rows']:
        n = row['native']
        assert n['stop_after_fine_same_ram'] and n['same_header_reuse_resets_clock'] and n['rejected_start_ram_unchanged']
        assert n['canonical_stop_observed'] and n['explicit_stop_before_fine_observed']
        assert sha(private / 'native-checked' / (row['game'] + '.log')) == row['log_sha256']
    a8 = next(x['native'] for x in native['rows'] if x['game'] == 'A8CJ')
    assert a8['alternate_stop_before_fine_unobserved'] and a8['alternate_stop_sink_delta'] == 0
    assert next(x['native'] for x in native['rows'] if x['game'] == 'AFEJ')['rejected_start_guarded_sink_emissions'] == 1
    assert reports['model']['passed'] and len(reports['model']['cases']) == 21 and not reports['model']['connected_to_product']
    assert all(x['uninterrupted_batches_exact'] and x['explicit_bridge_stop_honored'] for x in reports['counterfactual']['rows'])
    cross = [read(private / x / 'results.json') for x in ('cross-title','cross-title-A8CJ')]
    for title in [row for report in cross for row in report['rows']]:
        assert len(title['production_probe_equivalence']) == 3 and all(x['exact'] for x in title['production_probe_equivalence'])
        for speed in (1,2,3):
            observations = [x for x in title['observations'] if x['mode']=='experimental' and x['speed']==speed]
            assert len(observations)==2
            assert observations[0]['callback_sha256']==observations[1]['callback_sha256']
            assert observations[0]['events']==observations[1]['events']
    prior_aorj = {(x['mode'],x['speed']):x for x in read(root / 'build-phase6/final-stop-production/results.json')['runs']}
    fixed_aorj = [x for x in reports['aorj']['runs'] if x['mode'] == 'experimental']
    for run in reports['aorj']['runs']:
        old = prior_aorj[(run['mode'],run['speed'])]
        assert run['returncode'] == 0 and run['callback_sha256'] == old['callback_sha256'] and run['event_sha256'] == old['event_sha256']
    assert len(fixed_aorj) == 3
    aorj_notes = []
    for run in fixed_aorj:
        target = [fields(line) for line in run['voices'] if 'player=2 ' in line]
        requests = [{k:x[k] for k in ('sample','tick','track','key','priority','ch')} for x in target if x['decision']=='REQUEST']
        ends = [{k:x[k] for k in ('sample','track','key','priority','state','stop')} for x in target if x['decision']=='END']
        assert len(requests)==len(ends)==7
        assert int(requests[0]['sample'])==11782 and int(ends[-1]['sample'])==19043
        aorj_notes.append(dict(speed=run['speed'],requests=requests,ends=ends))
    assert all(x['requests']==aorj_notes[0]['requests'] and x['ends']==aorj_notes[0]['ends'] for x in aorj_notes)
    units = [json.loads(next(line for line in reversed((private / f'aorj-native-{speed}.log').read_text().splitlines()) if line.startswith('{'))) for speed in (2,3)]
    assert all(x['passed'] for x in units)
    frontend = [analyze(p.parent) for p in sorted((private / 'frontend').glob('*/results.json'))]
    assert len(frontend) == 9
    for run in frontend:
        f = run['report']
        assert f['returncode'] == 0 and f['frontend_muted'] and f['video_driver'] == 'null' and f['state_load_confirmed']
        assert f['ring_underrun_max'] == f['ring_overrun_max'] == 0 and not f['actual_windows_capture']
    repeats = [x for x in frontend if '2x-repeat' in x['path']]
    stall_again = any(i['wall_us'] - i['nominal_wall_us'] > 100000 for r in repeats for i in r['intervals'])
    assert not stall_again
    ar = next(x for x in frontend if 'AORJ-switch' in x['path'])
    previous, stall = [next(x for x in ar['intervals'] if x['begin_frame'] == frame) for frame in (840,960)]
    delta = {k:stall[k]-previous[k] for k in ('wall_us','transport_us','game_us','audio_including_callback_us','outside_measured_phases_us')}
    old_public = read(root / 'build-phase6/regression48/results.json')
    assert len(old_public['checks']) == 48 and sum(x['byte_exact'] for x in old_public['checks']) == 46
    for run in old_public['runs']:
        if run['label'].endswith('-v03'):
            assert sha(root / 'build-phase6/regression48' / run['label'] / 'callback.s16le') == run['callback_sha256']
    assert read(root / 'docs/three-x-phase6-evidence/regression48.json')['unexplained_difference_count'] == 0

    runtime = read(root / 'docs/three-x-phase6-evidence/runtime.json')
    probe = private / 'voice-probe/build/libmgba_mp2k_bridge.dll'
    assert all(sha(probe)==x['probe_sha256'] for x in cross)
    diagnostic_inputs = [p for folder in (private/'voice-probe/bridge',private/'voice-probe/agbplay/src/agbplay')
                         for p in folder.rglob('*') if p.is_file()]
    runtime.update(reused=True,new_production_dll=False,production_audio_source_changed=False,
        diagnostic_bridge=dict(path=str(probe),sha256=sha(probe),base_source_head=START,
            generator=str(root/'tools/public-integration/make_phase4_priority_probe.py'),
            generator_sha256=sha(root/'tools/public-integration/make_phase4_priority_probe.py'),
            copied_source_files=[dict(path=str(p),sha256=sha(p)) for p in sorted(diagnostic_inputs)],
            voice_ids='Private bridge-object lifetimes, not native channel generations',distribution=False))
    protection = read(root / 'build-phase7/protection-start.json')
    protection.update(protected_original_wavs_unchanged=103,new_protected_clips=12,total_protected_wavs=115,
        new_wavs=[x for x in listing['rows'] if x['new_recording']],new_audio_seconds=listing['new_audio_seconds'],
        deletions=False,moves=False,other_emulators_modified=False,production_audio_source_changed=False)
    out.mkdir(exist_ok=args.refresh_metadata)
    for name, report in reports.items():
        emit(out / (name + '.json'), report)
    emit(out/'cross-title.json',dict(reports=cross,production_probe_exact_conditions=12))
    emit(out/'native-aorj.json',dict(results=units,source_head=runtime['source_head'],scope='42 controlled native guard branches; not listening'))
    emit(out/'aorj-notes.json',dict(rows=aorj_notes,requests_and_ends_exact=True,whole_pcm_cross_speed_exact_not_claimed=True))
    emit(out/'three72-comparison.json',dict(rows=exact72,scope='New runs vs protected Phase6 production; no golden replacement'))
    emit(out/'guard-analysis.json',dict(checks=guard_checks,verified=26,not_applicable=2,injected_errors_not_audio_quality=True))
    emit(out/'public48-inherited.json',dict(strict_exact=46,justified_differences=2,unexplained=0,
        changed_cases=['AORJ-2x','AORJ-recovery'],scope='Retained public-core/common-bridge48 reference PCM rehashed; not freshly executed public full48. Fresh original-package/common-bridge16 scenes are separate.',
        reason='Existing Phase6 narrowly verified AORJ202 natural-completion handling; not new Phase8 changes.',source_sha256=sha(root/'docs/three-x-phase6-evidence/regression48.json')))
    frontend_record = dict(current=frontend,repeat2x_stall_over100ms=stall_again,
        prior192ms=read(root/'docs/THREE_X_PHASE7.json')['aorj_latency']['prior_stall_added_us'],
        new3x_stall=dict(previous=previous,stall=stall,delta=delta,cause='UNKNOWN; game-stage elapsed wall timer is not exclusive CPU time'),
        speed_counter=dict(measured='retro_run invocations per wall-clock second, not graphics FPS',
            mapping='One core->runFrame per retro_run. _GBACoreRunFrame advances until video.frameCounter changes, with a cycle timeout.',
            source=['src/platform/libretro/libretro.c:3239','src/platform/libretro/libretro.c:3291','src/gba/core.c:868'],
            limitation='Steady-state one-run/one-GBA-frame mapping is inferred from source. No independent frameCounter delta was recorded; timeout/recovery exceptions are not directly excluded. Strict emulated-frame speed certification remains INCONCLUSIVE.'),
        sample_counter='fixedSamples counts Audio Clock runAdvance while fixed-active; it is not a direct generated-PCM, callback-consumed or Windows-output sample counter.',
        physical_output='Muted/null-video; no new Windows endpoint recording or human listening')
    emit(out/'frontend.json',frontend_record)
    emit(out/'runtime.json',runtime)
    emit(out/'protection.json',protection)
    emit(out/'listening.json',listing)
    safety = dict(three_conditions=72,phase6_pcm_event_exact=72,ring_underrun_max=0,ring_overrun_max=0,
        queue_drop_max=0,queue_peak_max=max(x['queue_peak'] for x in three),
        expected_fallback_cases=[dict(case=x['label'],reasons=x['safety_fallback_reasons']) for x in three if x['safety_fallback_reasons']],
        guard_verified=26,guard_not_applicable=2,guards_weakened=False)
    human = [
        'Four finite programs: audible role, shortened endings, pitch-bend and timbre; automatic lifetime FAIL remains.',
        'AORJ202 final repeated notes and actual Windows1x/2x/3x output; no new human result.',
        'Cursor/confirm/cancel/battle/concurrent/burst/loop SFX, accepted replay and cancellation intent.',
        'Perceptual speed-switch and Load/Rewind continuity; ongoing SFX intentionally discarded.',
        'Six-title sustained actual Windows output, physical device underruns, BGM pitch/tempo; current frontend muted.',
        'A8CJ actively playing BGM fixture: present loaded fixture has no active players and is silent.',
    ]
    summary = dict(schema_version=1,phase=8,date='2026-10-10',
        git=dict(worktree=str(root),branch=git(root,'branch','--show-current'),start_head=START,
            final_head='See final local diagnostics/report commit; report cannot contain its own hash',push=False,tag=False,release=False),
        production_fix=False,generic_stop_implementation='NOT_ADOPTED_UNPROVED_CANCELLATION_AND_ACCEPTED_START_GENERATIONS',
        generation_model=dict(cases=21,passed=True,connected_to_product=False),runtime=runtime,
        cross_title=reports['lifetimes']['rows'],aorj202=dict(status='PASS_AUTOMATED_SCOPED',seven_requests_and_ends_exact_across_speeds=True,
            callback_and_event_exact_vs_phase6_each_speed=True,play_sample=10972,first_note_sample=11782,last_end_sample=19043,
            play_to_last_end_ms=(19043-10972)/32768*1000,human_review='HUMAN_REVIEW_REQUIRED'),
        b6jj='No production change; BGM/scenes/dedicated suites/3x matrix PASS; SFX hearing INCONCLUSIVE',
        regression=dict(new_phase6_candidate_exact=48,new_original_common_scene_exact=16,new_legacy_suites=12,
            new_three72_exact=72,public_common_full48_inherited_exact=46,public_common_full48_justified=2,unexplained=0,goldens_updated=False),
        safety=safety,aorj192ms=dict(status='UNKNOWN_INCONCLUSIVE',new_repeat_trials=2,reproduced=False,
            event_to_candidate_onset_samples=810,callback_coordinates_not_wall_delay=True,
            new3x_stall_delta_us=delta,production_change=False),
        frontend=dict(muted=True,new_windows_capture=False,runs=9,
            sample_rate_metric='Audio Clock advance per wall-clock second, not direct PCM/output-device rate',
            strict_emulated_frame_counter_speed='INCONCLUSIVE_NOT_DIRECTLY_RECORDED',
            switches=[dict(game=Path(x['path']).name.split('-')[0],segments=x['report']['segments'],one_core_cpu_percent=x['report']['one_core_cpu_percent']) for x in frontend if 'switch' in x['path']]),
        recovery=dict(ongoing_sfx_discarded=True,pending_tests=10,accepted_replay_unproved_cases=4,
            aamj_ownership_fallback_preserved=True,ffta_long_restore_runs=956),
        listening=dict(page=str(private/'listening-review-final/index.html'),references=31,reused=19,new_clips=12,
            new_seconds=listing['new_audio_seconds'],original_wavs_protected=103,total_protected=115,autoplay=False),
        human_review_required=human,release_decision='HOLD_V04_RC; FOUR_CONFIRMED_FINITE_PROGRAM_TRUNCATIONS',
        gates=dict(compatibility='PASS_SCOPED_REGRESSION',strict_emulated_3x_speed='INCONCLUSIVE_DIRECT_COUNTER_MISSING',
            bgm='PASS_AUTOMATED_PCM_INTERVALS_SCOPED; A8CJ_LOADED_SCENE_INCONCLUSIVE',sfx='FAIL_FOUR_TITLES',
            switches='PASS_AUTOMATED_SCOPED',recovery='PASS_SAFETY_SCOPED',safety='PASS_TESTED_GUARDS',hearing='HUMAN_REVIEW_REQUIRED'),
        evidence=[dict(path=str(f.relative_to(root)).replace('\\','/'),sha256=sha(f)) for f in sorted(out.glob('*.json'))])
    emit(root/'docs/THREE_X_PHASE8.json',summary)
    print('Phase8 metadata:48/48,16/16,12/12,72/72; four SFX lifetime failures retained; RC HOLD')


if __name__ == '__main__':
    main()
