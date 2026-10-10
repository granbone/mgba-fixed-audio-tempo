"""Separate emulation intervals, measured core phases and physical-audio evidence.
SPDX-License-Identifier: MPL-2.0.
"""
import json,re
from pathlib import Path
from test_v03_rc import FPS

def analyze(folder):
 report=json.loads((folder/'results.json').read_text());text=(folder/'frontend.log').read_text(errors='replace')
 probes={int(f):(int(t),int(s),int(k)) for f,t,s,k in re.findall(r'\[FIXED AUDIO PROBE\] frames=(\d+) us=(\d+) fixedSamples=(\d+) fixed=(\d)',text)}
 profiles={int(f):tuple(map(int,(t,g,a))) for f,t,g,a in re.findall(r'\[FIXED AUDIO PROFILE\] frames=(\d+) transportUs=(\d+) gameUs=(\d+) audioUs=(\d+)',text)}
 rows=[]
 for segment in report['segments']:
  keys=[p[1] for p in segment['probes']]
  for b,e in zip(keys,keys[1:]):
   if b not in profiles or e not in profiles:continue
   wall=probes[e][0]-probes[b][0];stages=[x-y for x,y in zip(profiles[e],profiles[b])]
   rows.append(dict(target_speed=segment['target_speed'],begin_frame=b,end_frame=e,wall_us=wall,nominal_wall_us=(e-b)/FPS/segment['target_speed']*1e6,
    measured_speed_x=(e-b)/FPS/(wall/1e6),fixed_sample_delta=probes[e][1]-probes[b][1],transport_us=stages[0],game_us=stages[1],audio_including_callback_us=stages[2],
    outside_measured_phases_us=wall-sum(stages)))
 return dict(path=str(folder),report=report,intervals=rows,
  semantics=[x for x in text.splitlines() if '[MP2K SEMANTIC]' in x and 'song=202 ' in x],
  timing_limit='Phase timers are elapsed wall time, including scheduling and I/O. Outside measured phases includes frontend throttling, diagnostic probe writes and other unmeasured work. Audio includes synthesis, polling and frontend callback; it is not an exclusive CPU profile.')

def main():
 r=Path.cwd();d=r/'build-phase7';current=[analyze(p.parent) for p in sorted((d/'frontend').glob('*/results.json'))]
 old=analyze(r/'build-phase6/windows-quiet-AORJ-2');triplet=[next(x for x in old['intervals'] if x['begin_frame']==frame) for frame in (360,480,600)]
 before,stall,after=triplet;delta={k:stall[k]-before[k] for k in ('wall_us','transport_us','game_us','audio_including_callback_us','outside_measured_phases_us')}
 assert delta['audio_including_callback_us']<2000 and delta['outside_measured_phases_us']>130000
 assert len(current)==9 and all(x['report']['state_load_confirmed'] and x['report']['frontend_muted'] for x in current)
 assert all(x['report']['ring_underrun_max']==x['report']['ring_overrun_max']==0 for x in current)
 repeated=[x for x in current if '2x-repeat' in x['path']]
 result=dict(current=current,prior_audible_stall=old,prior_stall_minus_previous_interval=delta,
  aorj_2x_repeat_measured_speed=[x['report']['segments'][0]['measured_speed_x'] for x in repeated],
  repeated_stall_over_100ms=any(x['wall_us']-x['nominal_wall_us']>100000 for run in repeated for x in run['intervals']),
  conclusion='Prior delay is a real emulation-interval stretch, not merely WAV phase. Most added wall time is outside measured core phases; the audio/queue/synthesis/callback phase grows by less than2ms. No deterministic SFX event/bridge onset delay or audio-only192ms stall was found. The OS/process/frontend/diagnostic source is unresolved; no production fix is justified.',
  listening='HUMAN_REVIEW_REQUIRED; current frontend runs are muted and never count as heard or newly captured Windows audio.')
 (d/'frontend-analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(stall_delta=delta,repeat_rates=result['aorj_2x_repeat_measured_speed'],repeat_stall=result['repeated_stall_over_100ms'])))

if __name__=='__main__':main()
