"""Create twelve private listening files and provenance without touching originals.
SPDX-License-Identifier: MPL-2.0. Silence separators/sample-rate conversion only;
never normalize volume, time stretch, pitch correct, overwrite, or delete a source.
"""
import argparse,hashlib,html,json,wave
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly

CORE='855A4EAB462AD2194A893A532399BA72D9A6872FC9703DDF393651220524FA9C'
BRIDGE='16344DFE15E687A68A7669E68C05DDD06E008FE8EC75DF932E5891BBBA28AF18'
BASE_CORE='A4BD66BA6A50CFC59109971DC1937BD3675991EE5E302FD3C1554F93C114BBF1'
BASE_BRIDGE='6C3EFC3F5FEDA53052A4432322A62357925842B79C54396479FFFCE857D29A40'

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.root.resolve()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    rows=[];before={r['path']:r['sha256'] for r in json.loads((root/'build-phase3/protected-originals.json').read_text())}
    def source(file,game,speed,start,seconds,scene,rate=None,core=CORE,bridge=BRIDGE,route='CORE_PCM'):
        file=root/file;digest=sha(file)
        if file.suffix=='.wav':
            with wave.open(str(file)) as w:
                sr=w.getframerate();channels=w.getnchannels();assert w.getsampwidth()==2
                x=np.frombuffer(w.readframes(w.getnframes()),'<i2').reshape(-1,channels)
        else:sr=rate;assert sr;x=np.fromfile(file,'<i2').reshape(-1,2)
        begin=round(start*sr);end=begin+round(seconds*sr);assert end<=len(x),(file,end,len(x))
        clip=x[begin:end].astype(float)
        if clip.shape[1]==1:clip=np.repeat(clip,2,axis=1)
        if sr!=48000:clip=resample_poly(clip,48000,sr,axis=0)
        meta=dict(source=str(file),source_sha256=digest,game=game,speed=speed,source_start_seconds=start,
            seconds=seconds,source_sample_rate=sr,scene=scene,core_sha256=core,bridge_sha256=bridge,
            route=route,source_head='91f153b4e7fd192872a892d81f37501b3a8d4304' if core==BASE_CORE else 'cd6af627ea8ef4e9b2d2e191e3e54fe527a6ecf6',
            bridge_provenance='private native-priority diagnostic; NOT production' if bridge not in (BRIDGE,BASE_BRIDGE) else 'original v0.3' if bridge==BASE_BRIDGE else 'Phase2 stable-order bridge')
        assert sha(file)==digest;return clip,meta
    def save(name,label,parts):
        pcm=[];segments=[];position=0.
        for i,(x,m) in enumerate(parts):
            if i:pcm.append(np.zeros((14400,2)));position+=.3
            m['review_start_seconds']=position;position+=len(x)/48000;segments.append(m);pcm.append(x)
        f=out/name;data=np.clip(np.rint(np.concatenate(pcm)),-32768,32767).astype('<i2')
        with wave.open(str(f),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(data.tobytes())
        rows.append(dict(file=str(f),sha256=sha(f),label=label,seconds=len(data)/48000,segments=segments,human_review='HUMAN_REVIEW_REQUIRED'))
    measured=json.loads((root/'build-phase3/windows-spectral-comparison.json').read_text())['records']
    for i,r in enumerate(measured):
        code=r['game'];start=r['windows'][0]['reference_start_seconds']
        scene='Phase2 loaded BGM reference scene; native state SHA in frontend results'
        save(f'{i*2+1:02}-{code}-1x-reference.wav',code+' 1x BGM基準',
            [source(Path(f'build-phase2/human-review/{code}-1x-core-private.wav'),code,1,start,7,scene)])
        save(f'{i*2+2:02}-{code}-3x-Windows.wav',code+' 3x 実Windows出力',
            [source(Path(f'build-phase3/live-{code}/windows-loopback.wav'),code,3,17,7,scene,route='WASAPI_ENDPOINT_MIX')])
    start=measured[2]['windows'][0]['reference_start_seconds']-988/32768
    save('09-AORJ-2x-reference.wav','AORJ 2x 988サンプル位相差を対応付けた基準',
        [source(Path('build-phase2/human-review/AORJ-2x-frontend-private.wav'),'AORJ',2,start,7,'Same loaded BGM, pre-mute frontend PCM',route='PRE_MUTE_CORE_PCM')])
    save('10-B6JJ-controlled-SE-123.wav','B6JJ カーソルSE 1x / 2x / 3x（BGMを含む）',
        [source(Path(f'build-phase2/human-review/B6JJ-{s}x-controlled-SE-private.wav'),'B6JJ',s,2.5,4,'Menu SE id122, audio-aligned triggers; mixed BGM',route='CONTROLLED_CORE_PCM') for s in (1,2,3)])
    parts=[]
    for code in ('AAMJ','AFXJ','AORJ','B6JJ'):
        r=json.loads((root/f'build-phase3/live-{code}/results.json').read_text())
        for stage in r['segments'][1:]:
            parts.append(source(Path(f'build-phase3/live-{code}/windows-loopback.wav'),code,'mixed',stage['begin']-1,3,
                'Continuous speed switch '+str(stage['target_speed'])+'x target',route='WASAPI_ENDPOINT_MIX'))
        suffix='-retry' if code=='B6JJ' else ''
        parts.append(source(Path(f'build-phase3/live-{code}-SE-recovery{suffix}/windows-loopback.wav'),code,'mixed',43,7,
            'Rewind then speed toggle, private saved scene; menu inputs attempted',route='WASAPI_ENDPOINT_MIX'))
    save('11-four-games-switch-and-recovery.wav','4タイトル：速度切替→Load/Rewind復帰の確認（順序はmanifest参照）',parts)
    probe=root/'build-phase3/priority-probe/build/libmgba_mp2k_bridge.dll'
    save('12-AORJ-PSG-priority-investigation.wav','AORJ：元v0.3→Phase2→非採用native優先度診断',[
        source(Path('build-phase2/aorj-package/PACKAGE-v03-1-0/callback.s16le'),'AORJ',1,16.8,1.7,'Boot/menu scripted song202 PSG conflict',rate=32768,core=BASE_CORE,bridge=BASE_BRIDGE),
        source(Path('build-phase2/aorj-package/PACKAGE-poc-1-0/callback.s16le'),'AORJ',1,16.8,1.7,'Same song202 conflict, production stable tie',rate=32768),
        source(Path('build-phase3/aorj-priority-native-priority/callback.s16le'),'AORJ',1,16.8,1.7,'Same conflict, isolated native combined-priority diagnostic',rate=32768,bridge=sha(probe))])
    assert len(rows)==12 and all(sha(Path(f))==h for f,h in before.items())
    (out/'manifest.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    page='<!doctype html><meta charset="utf-8"><title>非公開 Phase3 聴取</title><h1>非公開 Phase3 聴取素材</h1><p>自動再生なし。7秒BGM比較、SE、切替、AORJ診断。音量変更・時間伸縮・音程補正なし。聴取所見はゲームとファイル番号を付けて記録してください。</p>'
    for r in rows:
        page+=f'<section><h2>{html.escape(r["label"])}</h2><audio controls preload="none" src="{html.escape(Path(r["file"]).name)}"></audio><p>{html.escape(Path(r["file"]).name)} ({r["seconds"]:.1f}s)</p></section>'
    (out/'index.html').write_text(page,encoding='utf8');print('Created 12 private files; 27 originals unchanged:',out)

if __name__=='__main__':main()
