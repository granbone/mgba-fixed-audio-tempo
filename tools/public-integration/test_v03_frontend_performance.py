"""Real RetroArch paced CPU/speed comparison with isolated config/saves. MPL-2.0.
Control datagrams are sent to localhost on a separate port; the frontend's bind
behavior depends on its RetroArch version. No user configuration is changed.
Frame/time rates include startup and the initial normal-speed interval before fast-forward.
"""
import argparse,hashlib,json,os,re,socket,subprocess,time
from pathlib import Path
from test_v03_rc import cpu_seconds,FPS
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('retroarch','core','rom','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);before=hashlib.sha256(a.rom.read_bytes()).hexdigest()
 env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')};env['MGBA_FIXED_AUDIO_DIAGNOSTICS']='1'
 results=[]
 for mode in ('experimental','conservative','disabled'):
  for speed in (1,2):
   d=(a.output/(mode+'-'+str(speed))).resolve();d.mkdir();(d/'saves').mkdir();(d/'states').mkdir()
   options=d/'core-options.cfg';options.write_text('mgba_fixed_audio_tempo_mode = "'+mode+'"\n')
   settings={'video_driver':'d3d11','video_vsync':'false','audio_driver':'xaudio','audio_sync':'true','audio_mute_enable':'true',
    'audio_fastforward_mute':'false','pause_nonactive':'false','config_save_on_exit':'false','global_core_options':'true',
    'core_options_path':options.as_posix(),'rgui_config_directory':d.as_posix(),'system_directory':(d/'saves').as_posix(),
    'savefile_directory':(d/'saves').as_posix(),'savestate_directory':(d/'states').as_posix(),'libretro_info_path':d.as_posix(),
    'log_dir':d.as_posix(),'history_list_enable':'false','content_runtime_log':'false','content_runtime_log_aggregate':'false',
    'auto_overrides_enable':'false','auto_remaps_enable':'false','cheevos_enable':'false','savestate_auto_save':'false','savestate_auto_load':'false',
    'log_verbosity':'true','libretro_log_level':'0','network_cmd_enable':'true','network_cmd_port':'55493','network_cmd_bind_address':'127.0.0.1',
    'fastforward_ratio':'2.0','video_refresh_rate':str(FPS),'video_windowed_fullscreen':'false'}
   cfg=d/'retroarch.cfg';cfg.write_text(''.join(k+' = "'+v+'"\n' for k,v in settings.items()))
   log=d/'frontend.log';startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
   started=time.perf_counter();sent=None
   with (d/'process.log').open('wb') as capture:
    proc=subprocess.Popen([str(a.retroarch),'--config',str(cfg),'--verbose','--log-file',str(log),'--max-frames=900','-L',str(a.core),str(a.rom)],
     cwd=d,env=env,stdout=capture,stderr=subprocess.STDOUT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
    if speed==2:
     time.sleep(2)
     assert proc.poll() is None,'Frontend exited before toggle'
     with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:sock.sendto(b'FAST_FORWARD\n',('127.0.0.1',55493))
     sent=time.perf_counter()-started
    try:exitcode=proc.wait(timeout=60)
    except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
    cpu=cpu_seconds(proc)
   wall=time.perf_counter()-started;text=log.read_text(errors='replace')
   assert exitcode==0 and 'selected='+mode.capitalize() in text
   summary=dict(mode=mode,target_speed=speed,frames=900,wall_seconds=round(wall,4),cpu_seconds=round(cpu,4),
    one_core_cpu_percent=round(cpu/wall*100,3),achieved_average_speed_x=round(900/FPS/wall,4),
    toggle_after_seconds=round(sent,4) if sent else None,
    expected_seconds_excluding_startup=round(900/FPS/speed+(sent/2 if sent else 0),4),
    fixed_audio_active_observed='FIXED AUDIO ACTIVE' in text,
    fallback_reasons=sorted(set(re.findall(r'SAFE FALLBACK reason=([A-Z0-9_]+)',text))),
    underrun_count=max([int(x) for x in re.findall(r'underrun_count=(\d+)',text)] or [0]),
    overrun_count=max([int(x) for x in re.findall(r'overrun_count=(\d+)',text)] or [0]),
    speed_toggle_log_observed='ff=1' in text,
    physical_audio_underruns='NOT_INSTRUMENTED')
   results.append(summary);print(mode,speed,summary,flush=True)
 assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==before
 (a.output/'results.json').write_text(json.dumps(dict(passed=True,core_sha256=hashlib.sha256(a.core.read_bytes()).hexdigest().upper(),
  method='Serialized D3D11/XAudio2 muted 900-frame real RetroArch runs, 2x toggle sent at 2 seconds. Rates are boot-inclusive averages; per-mode CPU includes frontend/GPU-driver/audio work. Ring counters are core diagnostics, not physical audio-device underruns.',
  commands_source='https://docs.libretro.com/development/retroarch/network-control-interface/',records=results),indent=2)+'\n')
if __name__=='__main__':main()
