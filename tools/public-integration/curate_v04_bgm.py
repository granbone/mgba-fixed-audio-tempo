"""Short private comparison reels from existing PCM; originals are never modified.
SPDX-License-Identifier: MPL-2.0. No autoplay, scaling, normalization or publication.
"""
import html,json,wave
from pathlib import Path
from test_three_x_poc import sha
def main():
 root=Path.cwd();out=root/'build-phase10/listening';out.mkdir(parents=True,exist_ok=False)
 report=json.loads((root/'docs/GBA_BGM_COVERAGE_PHASE9.json').read_text(encoding='utf8'))
 analyses={r['identity']['game_code']:r['result'] for r in json.loads((root/'build-phase9/coverage-analysis.json').read_text(encoding='utf8'))['rows']}
 rows={r['identity']['game_code']:r for r in report['rows']}
 baseline=json.loads((root/'build-phase9/frontend-baseline-inputs/manifest.json').read_text(encoding='utf8'))['rows']
 codes=['AAMJ','AFXJ','AORJ','B6JJ','AXBJ','A2CJ','A9HJ','BFZJ','AE2J','ABFJ','ARJJ','BDDJ'];clips=[]
 for code in codes:
  rate=65536 if code=='B6JJ' else 32768
  identity=next(r for r in baseline if r['game_code']==code) if code in codes[:4] else rows[code]['identity']
  folder=root/'build-phase9/scenes16'/('phase4-'+code) if code in codes[:4] else root/analyses[code]['root']/'audio'/code/'fixed'
  portions=[];sources=[]
  for speed in (1,2,3):
   p=folder/str(speed)/'callback.s16le';b=p.read_bytes();offset=3*rate*4;portion=b[offset:offset+3*rate*4];assert len(portion)==3*rate*4
   if code not in codes[:4]:assert sha(p)==rows[code]['audio_sha256'][str(speed)]
   portions.append(portion);sources.append(dict(speed=speed,path=str(p.relative_to(root)),sha256=sha(p),source_begin_seconds=3,source_end_seconds=6,reel_begin_seconds=(speed-1)*3))
  target=out/(code+'-1x2x3x-private.wav')
  with wave.open(str(target),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(rate);w.writeframes(b''.join(portions))
  clips.append(dict(game=code,title=identity['title'],rom_sha256=identity['sha256'],revision=identity['revision'],rate=rate,path=target.name,sha256=sha(target),sources=sources,
   capture_core_sha256='DDC50B31DDB0D65B57C340AC5E168E1125005852184E2364E1F204DAE06184A4',bridge_sha256=report['runtime']['files'][0]['sha256'] if isinstance(report['runtime'],dict) and 'files' in report['runtime'] else 'D750BD152B51F632C37F627FC22C6A33E8CDA9B43B16294334D86E90DD511502',
   rc_relation='Phase9 candidate recordings; identical audio code to final RC, final12 prefix comparisons retained. Not Windows output capture.',human_review='HUMAN_REVIEW_REQUIRED'))
 (out/'manifest.json').write_text(json.dumps(dict(clips=clips,seconds=108,format='Unmodified PCM excerpts concatenated1x/2x/3x, each3seconds; intentional edit boundaries at3/6s, not speed-switch captures.',reference='First3seconds of each reel: Fixed1x. Native fidelity and instrument timbre require separate retained native clips.',protected=True),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 blocks=['<!doctype html><meta charset="utf-8"><title>Private v0.4 BGM review</title><h1>Private BGM comparison</h1><p>Each reel:1x0–3s;2x3–6s;3x6–9s. Manual play only. Compare phrase tempo, pitch, missing instruments and unintended gaps. Cuts at3/6s are excerpt boundaries. HUMAN_REVIEW_REQUIRED. No whole-game or SE certification.</p>']
 for c in clips:blocks.append(f'<h2>{c["game"]} — {html.escape(c["title"])}</h2><p>Limited saved scene; Fixed1x reference. Normal sample rate; no time stretch. {c["rate"]}Hz.</p><audio controls preload="none" src="{c["path"]}"></audio>')
 blocks.append('<p><a href="../../build-phase9/listening-review/index.html">Existing native1x and Fixed comparison clips</a> / <a href="../../build-phase8/listening-review-final/index.html">Retained earlier BGM/SE evidence</a></p>')
 (out/'index.html').write_text('\n'.join(blocks),encoding='utf8');print('12 private reels,108s total; originals unchanged')
if __name__=='__main__':main()
