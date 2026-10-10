"""Summarize observed lifetime shortening without authorizing general STOP suppression.
SPDX-License-Identifier: MPL-2.0.
"""
import json
from pathlib import Path

def main():
 r=Path.cwd();d=r/'build-phase7';loaded=json.loads((d/'cross-title-checked/results.json').read_text());boot=json.loads((d/'cross-title-A8CJ-boot/results.json').read_text());rows=[]
 for title in loaded['rows'][:3]+boot['rows']:
  case=title['case'];speeds=[]
  for speed in (1,2,3):
   run=next(x for x in title['observations'] if x['speed']==speed and x['instrumented_bridge'])
   play=next(x for x in run['events'] if x['type']=='PLAY_SONG');stop=next(x for x in run['events'] if x['type']=='STOP_SONG')
   starts=int(play['audioSample']);stops=int(stop['audioSample']);start_cycle=int(play['cycle']);stop_cycle=int(stop['cycle'])
   finish=[x for x in run['native'] if x['kind']=='NATIVE_FINISH' and start_cycle<int(x['cycle'])<=stop_cycle]
   cancel=[x for x in run['native'] if x['kind']=='NATIVE_STOP_CALL' and start_cycle<int(x['cycle'])<=stop_cycle]
   assert len(finish)==1 and not cancel and int(stop['seq'])>=1<<63
   # A8CJ later has song5 on the same player. Keep only ends belonging to the first song epoch.
   source=d/('cross-title-A8CJ-boot' if title['game']=='A8CJ' else 'cross-title-checked')/f'{title["game"]}-experimental-{speed}-probe/run.log'
   import re
   other_starts=[]
   for line in source.read_text(errors='replace').splitlines():
    if '[MP2K SEMANTIC]' in line and 'type=PLAY_SONG ' in line and f'player={case["player"]} ' in line:
     f=dict(re.findall(r'(\w+)=([^\s]+)',line));t=int(f['audioSample'])
     if t>starts:other_starts.append(t)
   boundary=min(other_starts,default=1<<63)
   ends=[int(x['sample']) for x in run['psg_voices'] if x['decision']=='END' and starts<=int(x['sample'])<boundary]
   assert ends
   speeds.append(dict(speed=speed,play_sample=starts,polled_stop_sample=stops,play_to_polled_stop_samples=stops-starts,
    native_finish_cycle=int(finish[0]['cycle']),native_finish_clock=int(finish[0]['clock']),native_finish_header=finish[0]['header'],
    explicit_stop_calls_between_start_and_finish=0,scope='Checked active SDK Stop PC only',
    voice_end_count=len(ends),last_voice_end_sample=max(ends),play_to_last_voice_end_samples=max(ends)-starts,
    play_to_last_voice_end_ms=(max(ends)-starts)/32768*1000,
    ring_underrun=run['underrun'],ring_overrun=run['overrun'],queue_drop=run['queue_drop']))
  ref=speeds[0]['play_to_last_voice_end_samples']
  for x in speeds:x['last_voice_duration_ratio_to_1x']=x['play_to_last_voice_end_samples']/ref
  assert all(x['last_voice_duration_ratio_to_1x']<.9 for x in speeds[1:])
  rows.append(dict(game=title['game'],name=title['name'],song=case['song'],player=case['player'],driver=title['driver'],rom_sha256=title['rom_sha256'],state_sha256=title['state_sha256'],from_reset=title.get('from_reset',False),speeds=speeds,
   status='FAIL_FINITE_PROGRAM_OR_VOICE_SHORTENED_IN_OBSERVED_SCENE',cancellation_intent='No checked explicit Stop call in the observed epoch; natural finish instruction precedes high-bit polling Stop.',
   audible_role='UNIDENTIFIED_SFX_OR_JINGLE; battle/cursor categorization needs gameplay review',
   general_stop_rollout='NOT_ENABLED; all-driver cancellation and accepted Start epoch coverage incomplete',
   phase7_fix=False,human_review='HUMAN_REVIEW_REQUIRED'))
 native_rows=json.loads((d/'cross-title-native/results.json').read_text())['rows']+json.loads((d/'cross-title-native-A8CJ/results.json').read_text())['rows']
 for row in rows:
  native=next(x for x in native_rows if x['game']==row['game']);header=row['speeds'][0]['native_finish_header']
  import re
  states=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in native['observations'][0]['native_lifetime'] if '[HOST LIFETIME PLAYER]' in line and f'header={header} ' in line]
  assert states
  finished=next(x for x in states if x['status']=='80000000');assert int(finished['clock'])==row['speeds'][0]['native_finish_clock']
  row['native_disabled_reference']=dict(first=states[0],finished=finished,clock_matches_fixed_native_finish=True,
   scope='Native Disabled memory transitions, not independent synthesis. A8CJ reset/input series reaches this header at frame480, while Fixed reaches it at660; compare per-program relative lifetime, not absolute request latency or identical UI state.')
 (d/'cross-title-analysis.json').write_text(json.dumps(dict(rows=rows,guards_weakened=False,
  rejected_scene='A8CJ loaded fixture requested song5 instead of target song0; original reset/input sequence was rerun and reproduced song0. Rejected scene retained.'),indent=2)+'\n')
 print(json.dumps(rows,indent=2))

if __name__=='__main__':main()
