"""Private25-second excerpts; actual-frame captions, no speed/audio manipulation.
SPDX-License-Identifier: MPL-2.0. Original recordings remain untouched.
"""
import ctypes,json,re,subprocess
from pathlib import Path
from test_gba_bgm_frontend_phase9 import FRAME
from test_three_x_poc import FPS,sha
def main():
 root=Path.cwd();out=root/'build-phase10/demos';out.mkdir(parents=True,exist_ok=False);rows=[]
 freq=ctypes.c_longlong();assert ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
 for game,folder in [('AFXJ','demo-ffta-portable-qt'),('AORJ','demo-oriental-fresh')]:
  d=root/'build-phase10'/folder;record=json.loads((d/'recording.json').read_text(encoding='utf8'));f=d/'frontend'/game
  r=json.loads((f/'results.json').read_text(encoding='utf8'));video=d/record['recording']
  changes=[(int(speed),int(qpc)/freq.value) for speed,qpc in re.findall(r'\[PRIVATE RC ADAPTER\] speed=(\d) accepted=1 qpc=(\d+)',(f/'process.log').read_text(errors='replace'))]
  assert [s for s,t in changes]==[2,3,1,2,3]
  begin=changes[0][1]-25/6;end=begin+25
  bounds=[(1,begin),*changes];points=[list(map(int,p)) for p in FRAME.findall((f/'frontend.log').read_text(errors='replace'))]
  measurements=[];subtitles=[]
  def stamp(t):
   n=round(max(0,t)*100);return f'{n//360000}:{n//6000%60:02}:{n//100%60:02}.{n%100:02}'
  for i,(speed,a) in enumerate(bounds):
   b=bounds[i+1][1] if i+1<len(bounds) else end
   q=[p for p in points if a+.1<p[1]/1e6<b-.1]
   actual=None;fps=None
   if len(q)>=2:
    x,y=q[0],q[-1];dt=(y[1]-x[1])/1e6;fps=(y[3]-x[3])/dt;actual=fps/FPS
   measurements.append(dict(target_speed=speed,begin_perf=a,end_perf=b,internal_speed_x=actual,gba_frames_per_second=fps,probe_count=len(q),internal_counter_samples=q))
   label=f'GBA actual {actual:.3f}x / {fps:.2f} frames/s' if actual is not None else 'GBA speed: insufficient samples'
   subtitles.append(f'Dialogue: 0,{stamp(a-begin)},{stamp(b-begin)},Default,,0,0,0,,{game} Fixed Audio RC | {label}\\NInternal GBA frames; overlay FPS is rendering only.\\NPrivate preview / HUMAN_REVIEW_REQUIRED')
  ass=out/(game+'-speeds.ass');ass.write_text('[Script Info]\nScriptType: v4.00+\nPlayResX: 960\nPlayResY: 640\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H90000000,0,0,0,0,100,100,0,0,3,1,0,2,10,10,12,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'+'\n'.join(subtitles)+'\n',encoding='utf8')
  start=begin-record['obs_creation_detected_perf'];assert 0<start<12
  target=out/(game+'-v04-25s-private.mp4')
  cmd=['C:/msys64/mingw64/bin/ffmpeg.exe','-hide_banner','-ss',str(start),'-i',str(video),'-t','25','-vf','subtitles='+ass.name,'-c:v','libx264','-preset','fast','-crf','20','-c:a','copy','-movflags','+faststart',str(target)]
  with (out/(game+'-encode.log')).open('xb') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
  probe=json.loads(subprocess.check_output(['C:/msys64/mingw64/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(target)]))
  assert 24.8<float(probe['format']['duration'])<25.3 and any(s['codec_type']=='audio' for s in probe['streams'])
  rows.append(dict(game=game,path=str(target.relative_to(root)),sha256=sha(target),source_video=str(video.relative_to(root)),source_sha256=sha(video),trim_start_seconds=start,duration_seconds=float(probe['format']['duration']),measurements=measurements,
   video='Real RetroArch D3D11/OBS window capture, visual frames inspected for correct title. Original Windows WASAPI AAC track copied; no audio replacement.',
   edits='Trim and measured-speed subtitles only. No video acceleration, stretching, pitch correction, normalization or audio filters.',
   alignment='Clip origin uses first actual2x API acceptance minus25/6s. OBS file-creation detection polled at50ms; captions can have roughly50ms origin uncertainty. Human audiovisual review required.',
   core_sha256=sha(root/'build-phase9/final/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(root/'build-phase9/final/runtime/libmgba_mp2k_bridge.dll'),human_review='HUMAN_REVIEW_REQUIRED',public=False))
 (out/'manifest.json').write_text(json.dumps(dict(rows=rows,failed_takes_retained=True,failed_generic_window_take='demo-ffta was excluded because it captured another RetroArch window. No other emulator input or development changes were made.',publication='PROHIBITED; private review only'),indent=2)+'\n',encoding='utf8');print([(r['game'],r['duration_seconds'],[(round(s['internal_speed_x'],4) if s['internal_speed_x'] else None) for s in r['measurements']]) for r in rows])
if __name__=='__main__':main()
