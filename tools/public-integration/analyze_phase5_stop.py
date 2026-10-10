"""Bounded STOP-origin and SE lifetime evidence; no playback or golden updates.
SPDX-License-Identifier: MPL-2.0.
"""
import argparse,json,re,hashlib
from pathlib import Path
from test_three_x_poc import sha

def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('before','after','production','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();before=json.loads((a.before/'results.json').read_text());after=json.loads((a.after/'results.json').read_text());prod=json.loads((a.production/'results.json').read_text());rows=[]
 def requests(r):return [{k:fields(x)[k] for k in ('sample','tick','player','track','key','priority','ch')} for x in r['voices'] if 'decision=REQUEST ' in x and 'player=2 track=0 ' in x]
 def ends(r):return [fields(x) for x in r['voices'] if 'decision=END player=2 track=0 ' in x]
 reference=requests(after['runs'][0]);end_reference=ends(after['runs'][0])
 for speed in (1,2,3):
  old=before['runs'][speed-1];new=after['runs'][speed-1];production=prod['runs'][speed-1]
  assert new['callback_sha256']==production['callback_sha256']
  assert new['event_sha256']==production['event_sha256']
  native=[fields(x) for x in new['evidence'] if 'kind=NATIVE_FINISH' in x and 'player=2 ' in x]
  calls=[fields(x) for x in new['evidence'] if 'kind=NATIVE_STOP_CALL' in x and 'player=2 ' in x]
  oldplay=next(fields(x) for x in old['evidence'] if 'type=PLAY_SONG song=202 ' in x)
  oldstop=next(fields(x) for x in old['evidence'] if 'type=STOP_SONG song=202 ' in x)
  row=dict(speed=speed,play_sample=int(oldplay['audioSample']),old_stop_sample=int(oldstop['audioSample']),
   old_play_to_stop_ms=(int(oldstop['audioSample'])-int(oldplay['audioSample']))/32.768,
   game_cycles_play_to_polled_stop=int(oldstop['cycle'])-int(oldplay['cycle']),
   native_finish=native,native_explicit_stop_calls=calls,requests_before=requests(old),requests_after=requests(new),
   ends_before=ends(old),ends_after=ends(new),requests_exact_1x=requests(new)==reference,ends_exact_1x=ends(new)==end_reference,
   before_after_pcm_exact=old['callback_sha256']==new['callback_sha256'],before_after_event_exact=old['event_sha256']==new['event_sha256'],
   probe_production_pcm_event_exact=True,status='PASS_LIMITED_SE202' if requests(new)==reference and ends(new)==end_reference else 'FAIL_INHERITED_TRUNCATION')
  rows.append(row)
 assert rows[0]['before_after_pcm_exact'] and rows[1]['before_after_pcm_exact']
 assert rows[2]['requests_exact_1x'] and rows[2]['ends_exact_1x'] and not rows[2]['native_explicit_stop_calls']
 assert all(len(r['native_finish'])==1 and r['native_finish'][0]['clock']=='16' for r in rows)
 result=dict(inputs={str(f):sha(f) for f in [a.before/'results.json',a.after/'results.json',a.production/'results.json']},rows=rows,
  stop_origin='Frame-level polling translates native natural completion into MPLAY_STOP; no native Stop call in the watched interval.',
  scope='Only exact AORJ identity / player2 / SE202 / verified clock16 FINE / supported3x. Existing1x and2x deliberately unchanged.',
  hypotheses=dict(A='REJECTED: correct player2/track0, no owner mismatch in fixture',B='REJECTED_IN_FIXTURE: no BGM stop at this SE boundary',C='CONFIRMED_REVISED: native completion advances on game time; synthetic polling STOP arrives sooner',D='REJECTED_AS_TERMINAL_CAUSE: early-note replacement is legitimate; premature terminal Kill follows synthetic STOP',E='No timestamp-conversion error found: 4494336 game cycles map to8778/4389/2926 audio samples; independent note spacing remains548',F='No stale STOP in clean fixture; recovery suites and generation-reset test recorded separately'),
  human_review_required=['Audible timbre/pitch continuity','Battle and loop SFX','Concurrent SFX and pitch bend','Perceptual Load/Rewind continuity'])
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps([{k:r[k] for k in ('speed','status','requests_exact_1x','ends_exact_1x')} for r in rows]))
if __name__=='__main__':main()
