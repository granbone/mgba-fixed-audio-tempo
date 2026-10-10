"""Twelve bounded callback clips plus retained manual references; no playback.
SPDX-License-Identifier: MPL-2.0. No retiming, gain, pitch correction or originals overwritten.
"""
import argparse,html,json,re,wave
from pathlib import Path
from test_three_x_poc import sha

def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--resume',action='store_true',help='Finish incomplete curation, validating existing clips without overwriting')
    a=p.parse_args();r=Path.cwd();d=a.output.resolve()
    if a.resume:assert d.is_dir() and not (d/'manifest.json').exists() and not (d/'index.html').exists()
    else:d.mkdir(parents=True,exist_ok=False)
    inherited=json.loads((r/'build-phase7/listening-review/manifest.json').read_text(encoding='utf8'));rows=inherited['rows']
    for row in rows:
        assert sha(Path(row['path']))==row['sha256']
        row['review_phase']=8;row['new_recording']=False
    # Revalidate the 12 BGM original PCM bodies against newly rerun scenes.
    for row in rows[:12]:
        records=json.loads((r/f'build-phase8/scenes16/phase4-{row["game"]}/results.json').read_text())['records']
        with wave.open(row['path'],'rb') as w:
            import hashlib
            assert hashlib.sha256(w.readframes(w.getnframes())).hexdigest().upper()==records[row['speed']-1]['callback_sha256']
        row['phase8_scene_pcm_exact']=True
    for title in json.loads((r/'build-phase8/lifetime-analysis.json').read_text(encoding='utf8'))['rows']:
        code=title['game'];base=r/'build-phase8'/('cross-title-A8CJ' if code=='A8CJ' else 'cross-title')
        for measure in title['speeds']:
            speed=measure['speed'];folder=base/f'{code}-experimental-{speed}-production';text=(folder/'run.log').read_text(errors='replace')
            clocks=[fields(x) for x in text.splitlines() if '[AUDIO CLOCK] run=' in x]
            callbacks={int(f['frame']):f for f in (fields(x) for x in text.splitlines() if '[HOST CALLBACK]' in x)}
            av=[fields(x) for x in text.splitlines() if '[HOST] av_rate=' in x]
            runs={int(f['run']):f for f in (fields(x) for x in text.splitlines() if '[FIXED AUDIO RUN]' in x)}
            desired=measure['play_sample']-1638  # 50ms context before the semantic request
            clock=next(x for x in clocks if int(x['sample'])-int(x['advance'])<=desired<int(x['sample']))
            run=int(clock['run']);callback=callbacks[run-1]
            assert int(callback['samples'])==int(clock['advance'])
            offset=int(callback['byteOffset'])//4+desired-(int(clock['sample'])-int(clock['advance']))
            count=min(round(2.4*32768),(folder/'callback.s16le').stat().st_size//4-offset)
            assert count>=round(1.85*32768)  # short3x reset run still covers the target's end
            seconds=count/32768;end=offset+count
            for frame,c in callbacks.items():
                lo=int(c['byteOffset'])//4;hi=lo+int(c['samples'])
                if lo<end and hi>offset:
                    rate=next(int(x['av_rate']) for x in reversed(av) if int(x['frame'])<=frame)
                    assert rate==32768 and int(runs[frame+1]['startup'])==0 and int(runs[frame+1]['fallback'])==0
            with (folder/'callback.s16le').open('rb') as f:f.seek(offset*4);data=f.read(count*4)
            name=f'{code}-song{title["song"]}-{speed}x-observed-shortening.wav';file=d/name
            if file.exists():
                assert a.resume
                with wave.open(str(file),'rb') as w:assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(2,2,32768) and w.readframes(w.getnframes())==data
            else:
                with wave.open(str(file),'wb') as w:w.setparams((2,2,32768,0,'NONE','not compressed'));w.writeframes(data)
            rows.append(dict(path=str(file),sha256=sha(file),game=code,name=title['title'],kind=f'有限音声 song{title["song"]} / 未修正の寿命比較',speed=speed,begin=0,end=seconds,
                reference=f'{code} 同headerのFixed1x。nativeとの完全音色一致やSE用途は未認定。',
                attention=f'1xの末尾と2x/3xの短縮・欠落を比較。発音間隔と個々の長さを分けて聴く。自動寿命判定はFAIL（{measure["last_voice_end_ms"]:.3f}ms）。',
                new_recording=True,recording_kind='Exported headless libretro callback, not Windows endpoint',source=str(folder/'callback.s16le'),source_sha256=sha(folder/'callback.s16le'),
                source_sample_begin=offset,source_samples=count,semantic_audio_sample=measure['play_sample'],audio_clock_context_begin=desired,
                mapping='Fixed-active run start → HOST callback index; clip rates32768 verified. No optimized phase alignment or waveform retiming. Render lookahead and actual attack are distinct.',
                rom_sha256=title['rom_sha256'],state_sha256=title['fixture_sha256'],from_reset=title['from_reset'],
                core_sha256=sha(r/'build-phase6/final/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(r/'build-phase6/final/runtime/libmgba_mp2k_bridge.dll'),
                source_head='d08add4741f454f3a459343067b0bb275e998e34',human_review='HUMAN_REVIEW_REQUIRED',autoplay=False))
    assert len(rows)==31 and len(list(d.glob('*.wav')))==12
    manifest=dict(rows=rows,reused_references=19,new_wavs=12,new_audio_seconds=sum(x['end'] for x in rows if x['new_recording']),autoplay=False,protected_originals_unchanged=True)
    (d/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    cards=[]
    for row in rows:
        label=f'{row["game"]} — {row["kind"]} {row["speed"]}x'
        cards.append(f'<article><h2>{html.escape(label)}</h2><p>基準: {html.escape(row["reference"])}<br>{html.escape(row["attention"])}</p><p>HUMAN_REVIEW_REQUIRED</p><audio controls preload="none" data-begin="{row["begin"]}" data-end="{row["end"]}" src="{html.escape(Path(row["path"]).as_uri(),quote=True)}"></audio><details><summary>出典・DLL・SHA256</summary><pre>{html.escape(json.dumps(row,ensure_ascii=False,indent=2))}</pre></details></article>')
    page='''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Phase8 非公開聴取比較</title>
<style>body{max-width:1000px;margin:32px auto;padding:0 20px;font:16px/1.65 system-ui;background:#f5f6f8;color:#17202b}article{background:white;border:1px solid #ccd2da;border-radius:8px;margin:18px 0;padding:18px}h2{font-size:19px}audio{width:100%}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}</style>
<h1>Phase8 非公開聴取比較</h1><p>ボタンを押したときだけ再生します。製品STOP処理は未変更。4タイトルの寿命短縮は未修正です。</p>
<p>既存19参照を再利用。4タイトル×3速度の新規12素材は最大2.4秒のheadless callback出力で、Windows録音ではありません。SE／ジングルの用途、音程、発音順は人の確認が必要です。A8CJは同headerの相対比較で、nativeと同一UI時点を保証しません。</p>
'''+''.join(cards)+'''<script>
const sounds=[...document.querySelectorAll('audio')];
for(const a of sounds){a.addEventListener('play',()=>{for(const b of sounds)if(b!==a)b.pause();if(a.currentTime<+a.dataset.begin||a.currentTime>=+a.dataset.end)a.currentTime=+a.dataset.begin;});a.addEventListener('timeupdate',()=>{if(a.currentTime>=+a.dataset.end)a.pause();});}
</script></html>'''
    assert page.count('<audio ')==31 and 'autoplay=' not in page
    (d/'index.html').write_text(page,encoding='utf8');print('19 reused references +12 clips up to2.4s; no playback; originals unchanged')

if __name__=='__main__':main()
