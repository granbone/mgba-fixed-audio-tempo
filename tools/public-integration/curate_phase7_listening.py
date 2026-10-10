"""Create an offline, manual listening page referencing protected existing WAVs.
SPDX-License-Identifier: MPL-2.0. No new WAV, autoplay, upload or original edits.
"""
import hashlib,html,json,wave
from pathlib import Path
from test_three_x_poc import sha

def main():
 r=Path.cwd();d=r/'build-phase7/listening-review';d.mkdir(parents=True,exist_ok=False)
 old=json.loads((r/'docs/three-x-phase6-evidence/protection.json').read_text());originals={x['path']:x for x in old['protected_wavs']};rows=[]
 names={'AAMJ':'悪魔城ドラキュラ Circle of the Moon','AFXJ':'Final Fantasy Tactics Advance','AORJ':'オリエンタルブルー','B6JJ':'スーパーロボット大戦J'}
 for code in names:
  measurement=json.loads((r/f'build-phase7/scenes16/{code}-bgm.json').read_text());records=json.loads((r/f'build-phase7/scenes16/phase4-{code}/results.json').read_text())['records']
  for speed in (1,2,3):
   file=r/f'build-phase2/human-review/{code}-{speed}x-core-private.wav';meta=originals[str(file)]
   assert sha(file)==meta['sha256']
   with wave.open(str(file),'rb') as w:rate=w.getframerate();data=w.readframes(w.getnframes())
   # Entire protected recording equals the current Phase7 callback output, not just a hash of a cut.
   exact=hashlib.sha256(data).hexdigest().upper()==records[speed-1]['callback_sha256'];assert exact
   offset=0 if speed==1 else next(x for x in measurement['comparisons'] if x['speed']==speed)['windows'][1]['offset_samples']/rate
   begin=5-offset;rows.append(dict(path=str(file),sha256=sha(file),game=code,name=names[code],kind='BGM',speed=speed,begin=begin,end=begin+7,
    reference=f'{code} 1x',attention='同じフレーズの進行・音程・音色・途切れ。SE品質とは別の項目です。',
    original_provenance=meta,current_scene_pcm_exact=exact,current_core_sha256=sha(r/'build-phase6/final/runtime/mgba_fixed_audio_libretro.dll'),
    current_bridge_sha256=sha(r/'build-phase6/final/runtime/libmgba_mp2k_bridge.dll'),human_review='HUMAN_REVIEW_REQUIRED'))
 for meta in json.loads((r/'build-phase6/listening-review/manifest.json').read_text()):
  file=Path(meta['path']);assert sha(file)==meta['sha256']
  rows.append(dict(path=str(file),sha256=sha(file),game='AORJ',name=names['AORJ'],kind='SE202 / Windows比較' if 'Windows' in file.name else 'SE202 / 修正前後',
   speed=meta['speed'],begin=0,end=meta['seconds'],reference='Phase6 1x SE202。04は修正前の既知の途中終了。',
   attention='最後の繰り返し高音、個々の長さ・音程、途切れ・重複。Windows 2xは過去に一時停止した収録で、正常基準には使わない。',
   original_provenance=meta,human_review='HUMAN_REVIEW_REQUIRED'))
 manifest=d/'manifest.json';manifest.write_text(json.dumps(dict(rows=rows,new_wavs=0,autoplay=False,source_wavs_read_only=True),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 cards=[]
 for i,row in enumerate(rows):
  path=Path(row['path']);url=path.as_uri();label=f'{row["game"]} {row["name"]} — {row["kind"]} {row["speed"]}x'
  cards.append(f'<article><h2>{html.escape(label)}</h2><p>基準: {html.escape(row["reference"])}<br>{html.escape(row["attention"])}</p><p>区間 {row["begin"]:.3f}–{row["end"]:.3f}秒 / HUMAN_REVIEW_REQUIRED</p><audio controls preload="none" data-begin="{row["begin"]}" data-end="{row["end"]}" src="{html.escape(url,quote=True)}"></audio><details><summary>出典・DLL・SHA256</summary><pre>{html.escape(json.dumps(row,ensure_ascii=False,indent=2))}</pre></details></article>')
 page='''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Phase7 非公開聴取比較</title>
<style>body{max-width:1000px;margin:32px auto;padding:0 20px;font:16px/1.65 system-ui;background:#f5f6f8;color:#17202b}article{background:white;border:1px solid #ccd2da;border-radius:8px;margin:18px 0;padding:18px}h2{font-size:19px}audio{width:100%}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}</style>
<h1>Phase7 非公開聴取比較</h1><p>再生ボタンを押したときだけ音が出ます。1x/2x/3xは同じBGMフレーズの7秒区間です。原本は変更していません。</p>
<p>BGM 12項目は既存Phase2 WAVを再利用し、今回のDLLによる同じ場面のPCMとの全体一致を確認済み。AORJ SE202はPhase6の短い7素材を再利用します。ゲーム側の発音間隔とSE自身の寿命を区別してください。</p>
<p>自動試験のPASSは聴取のPASSではありません。AFXJ・AFEJ・AAKJ・A8CJの有限音声には別途、途中終了の未修正問題があります。これらの音程・ループ・戦闘・同時発音は未聴取です。</p>
'''+''.join(cards)+'''<script>
const sounds=[...document.querySelectorAll('audio')];
for(const a of sounds){a.addEventListener('play',()=>{for(const b of sounds)if(b!==a)b.pause();if(a.currentTime<+a.dataset.begin||a.currentTime>=+a.dataset.end)a.currentTime=+a.dataset.begin;});a.addEventListener('timeupdate',()=>{if(a.currentTime>=+a.dataset.end)a.pause();});}
</script></html>'''
 (d/'index.html').write_text(page,encoding='utf8')
 assert page.count('<audio ')==19 and 'autoplay=' not in page
 print('19 references (12 BGM +7 existing SE/Windows), zero new WAVs; no playback')

if __name__=='__main__':main()
