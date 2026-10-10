"""Bounded, isolated audible Windows RetroArch/WASAPI switch captures.
SPDX-License-Identifier: MPL-2.0. Never changes system volume or other processes.
"""
import argparse, json, os, re, socket, struct, subprocess, time, wave
from pathlib import Path
import numpy as np
import pyaudiowpatch as pa
from test_three_x_retroarch import sha
from test_v03_rc import FPS, cpu_seconds

PROBE = re.compile(r'\[FIXED AUDIO PROBE\] frames=(\d+) us=(\d+) fixedSamples=(\d+) fixed=(\d)')


def state_loaded(text, mode):
    if 'STATE_LOAD_DETECTED' in text or '[B6JJ RECOVERY] LOAD' in text:return True
    # Disabled has no fixed-audio recovery markers. The valid isolated state's
    # core loading sequence, rather than a fixed-route marker, confirms loading.
    return mode=='disabled' and 'Savestate: Loading savedata' in text and 'Savestate: Loading RTC' in text

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core', 'bridge', 'rom', 'state', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--retroarch', type=Path, default=Path('C:/RetroArch-Win64/retroarch.exe'))
    p.add_argument('--schedule', default='1,3,1,3')
    p.add_argument('--segment-seconds', type=float, default=12)
    p.add_argument('--port', type=int, default=56583)
    p.add_argument('--mode', choices=('experimental', 'disabled'), default='experimental')
    routing = p.add_mutually_exclusive_group(required=True)
    routing.add_argument('--audible', action='store_true',
        help='Explicitly enable speaker/headphone output for this test. Analysis/curation tools are silent.')
    routing.add_argument('--muted', action='store_true', help='Background frontend/recovery tests; no physical output quality claim.')
    p.add_argument('--compact-capture', action='store_true', help='Store a 48kHz listening copy of endpoint capture, without time/pitch scaling.')
    p.add_argument('--se-actions', action='store_true', help='Remote RetroPad cursor/confirm/cancel/short burst schedule; scene effects must be verified from cue logs')
    p.add_argument('--single-se', action='store_true', help='One A press 0.4s after segment start, for a separately loaded, identified SE fixture')
    p.add_argument('--incremental-log', action='store_true', help='Read only newly appended frontend log bytes; same core/audio settings')
    p.add_argument('--no-pcm-capture', action='store_true', help='Keep diagnostic text only; no new callback/backend PCM files')
    p.add_argument('--recovery', action='store_true', help='Save at 1x, load at 3x, rewind in final 3x segment and toggle immediately afterwards')
    p.add_argument('--adapter',type=Path,help='Private forwarding adapter for real frontend 1/2/3 override; never included in RC')
    p.add_argument('--video',action='store_true',help='D3D11 window, shown without activation for private capture')
    p.add_argument('--video-driver',choices=['d3d11','gdi'],default='d3d11')
    a = p.parse_args()
    speeds = list(map(int, a.schedule.split(',')))
    assert set(speeds) <= {1,2,3} and (a.adapter or len(set(speeds)-{1}) <= 1)
    assert not a.recovery or speeds==[1,3,1,3], 'Recovery schedule requires 1,3,1,3'
    assert not a.single_se or (len(speeds)==1 and not a.se_actions and not a.recovery)
    assert not (a.recovery or a.se_actions) or a.segment_seconds>=10, 'Action schedule requires at least 10s segments'
    d = a.output.resolve(); d.mkdir(parents=True, exist_ok=False)
    inputs = {str(f.resolve()):sha(f) for f in (a.core,a.bridge,a.rom,a.state,a.retroarch)}
    if a.adapter:inputs[str(a.adapter.resolve())]=sha(a.adapter)
    speed_file=d/'frontend-speed.txt';speed_file.write_text('1')
    for name in ('states','saves','system'): (d/name).mkdir()
    (d/'states'/(a.rom.stem+'.state')).write_bytes(a.state.read_bytes())
    opt = d/'core-options.cfg'; opt.write_text(f'mgba_fixed_audio_tempo_mode = "{a.mode}"\n')
    (d/(a.core.stem+'.info')).write_text('display_name = "Private audible mGBA test"\ncorename = "mGBA"\nsupported_extensions = "gba"\nsavestate = "true"\n')
    if a.adapter:
        (d/(a.adapter.stem+'.info')).write_bytes((d/(a.core.stem+'.info')).read_bytes())
    settings = dict(video_driver='null',menu_driver='rgui',audio_driver='xaudio',audio_out_rate='48000',
        audio_latency='64',audio_mute_enable='true' if a.muted else 'false',audio_fastforward_mute='false',audio_sync='true',
        audio_rate_control='true',video_vsync='false',video_refresh_rate=str(FPS),
        pause_nonactive='false',config_save_on_exit='false',global_core_options='true',
        core_options_path=opt.as_posix(),libretro_info_path=d.as_posix(),
        libretro_directory=(a.adapter or a.core).resolve().parent.as_posix(),system_directory=(d/'system').as_posix(),
        savefile_directory=(d/'saves').as_posix(),savestate_directory=(d/'states').as_posix(),
        history_list_enable='false',content_runtime_log='false',content_runtime_log_aggregate='false',
        auto_overrides_enable='false',auto_remaps_enable='false',cheevos_enable='false',
        savestate_auto_load='false',savestate_auto_save='false',state_slot='0',
        network_cmd_enable='true',network_cmd_port=str(a.port),network_cmd_bind_address='127.0.0.1',
        fastforward_ratio=str(float(max(speeds))),input_driver='sdl2',input_joypad_driver='null',
        ui_companion_start_on_boot='false',log_verbosity='true',libretro_log_level='1',quit_press_twice='false')
    if a.video:settings.update(video_driver=a.video_driver,video_windowed_fullscreen='false',video_fullscreen='false',video_scale='3',video_window_save_positions='false',video_font_enable='false',video_smooth='false')
    if a.se_actions or a.single_se:
        settings.update(network_remote_enable='true',network_remote_base_port=str(a.port+10),network_remote_enable_user_p1='true')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',a.port+10))
    if a.recovery:settings.update(rewind_enable='true',rewind_buffer_size='64',rewind_granularity='1')
    cfg=d/'retroarch.cfg';cfg.write_text(''.join(f'{k} = "{v}"\n' for k,v in settings.items()))
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.bind(('127.0.0.1',a.port))
    env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
    env.update(MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()),MGBA_FIXED_AUDIO_WALL_PROBE='1',
        MGBA_FIXED_AUDIO_OUTPUT_PATH=str(d/'fixed-callback.s16le'),MGBA_MP2K_AUDIO_TRACE='1',
        MGBA_MP2K_AUDIO_TRACE_PATH=str(d/'backend.s16le'),MGBA_B6JJ_EVENT_TRACE='1',
        MGBA_MP2K_STARTUP_TRACE_PATH=str(d/'startup.log'))
    if a.no_pcm_capture:
        for key in ('MGBA_FIXED_AUDIO_OUTPUT_PATH','MGBA_MP2K_AUDIO_TRACE_PATH'):
            env.pop(key,None)
        env.pop('MGBA_MP2K_AUDIO_TRACE',None)
    if a.single_se:env.update(MGBA_MP2K_PSG_OWNER_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1')
    if a.adapter:env.update(MGBA_RC_ACTUAL_CORE=str(a.core.resolve()),MGBA_RC_SPEED_FILE=str(speed_file))
    audio=pa.PyAudio() if a.audible else None
    dev=audio.get_default_wasapi_loopback() if audio else None
    rate=int(dev['defaultSampleRate']) if dev else 48000
    channels=int(dev['maxInputChannels']) if dev else 2
    parts=[];statuses=[];capture_times=[]
    def cb(data,count,info,status):
        parts.append(data);statuses.append(status);capture_times.append([time.perf_counter(),count,info])
        return None,pa.paContinue
    stream=audio.open(format=pa.paInt16,channels=channels,rate=rate,input=True,
        input_device_index=dev['index'],frames_per_buffer=int(rate*.02),stream_callback=cb) if audio else None
    start=time.perf_counter();commands=[];observations=[];segment_start=None;stage=0;last_load=-10;done=set();button_release=[]
    def command(name):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.sendto((name+'\n').encode(),('127.0.0.1',a.port))
        commands.append(dict(command=name,elapsed=time.perf_counter()-start))
    def speed_change(target):
        if a.adapter:
            temp=speed_file.with_suffix('.tmp');temp.write_text(str(target))
            # The private adapter briefly opens the control file each run.
            # Windows can deny replacement while that read handle is open.
            deadline=time.perf_counter()+1;retries=0
            while True:
                try:os.replace(temp,speed_file);break
                except PermissionError:
                    if time.perf_counter()>=deadline:raise
                    retries+=1;time.sleep(.005)
            if retries:commands.append(dict(command='CONTROL_FILE_RETRY',retries=retries,elapsed=time.perf_counter()-start))
            commands.append(dict(command='FRONTEND_OVERRIDE_REQUEST',speed=target,elapsed=time.perf_counter()-start))
        else:command('FAST_FORWARD')
    def button(key,value):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
            s.sendto(struct.pack('<iiiiH2x',0,1,0,key,value),('127.0.0.1',a.port+10))
        commands.append(dict(command='RETROPAD',id=key,value=value,elapsed=time.perf_counter()-start))
    log=d/'frontend.log';proc=None;rc=None;log_cost=[];log_position=0;text=''
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    if a.video:startup.wShowWindow=4 # SW_SHOWNOACTIVATE; capture only this test window
    # Hard frame bound protects against dropped network commands and hangs.
    limit=int(FPS*(sum(speeds)*a.segment_seconds+10*max(speeds)))
    args=[str(a.retroarch),'--config',str(cfg),'--verbose','--log-file',str(log),
        '--max-frames='+str(limit),'-L',str((a.adapter or a.core).resolve()),str(a.rom.resolve())]
    try:
        with (d/'process.log').open('wb') as out:
            proc=subprocess.Popen(args,cwd=d,env=env,stdout=out,stderr=subprocess.STDOUT,startupinfo=startup)
            while proc.poll() is None and time.perf_counter()-start < len(speeds)*a.segment_seconds+12:
                elapsed=time.perf_counter()-start
                read_begin=time.perf_counter()
                if a.incremental_log:
                    if log.exists():
                        with log.open('rb') as reader:
                            reader.seek(log_position);added=reader.read();log_position=reader.tell()
                        text+=added.decode('utf8',errors='replace')
                else:text=log.read_text(errors='replace') if log.exists() else ''
                probes=PROBE.findall(text)
                log_cost.append(time.perf_counter()-read_begin)
                if probes:observations.append([elapsed,*map(int,probes[-1])])
                loaded=state_loaded(text,a.mode)
                if not loaded and '[NetCMD]' in text and elapsed>1 and elapsed-last_load>1:
                    command('LOAD_STATE');last_load=elapsed
                if loaded and segment_start is None and elapsed>3:
                    segment_start=elapsed
                    if speeds[0]>1:speed_change(speeds[0])
                    commands.append(dict(command='SEGMENT_BEGIN',speed=speeds[stage],elapsed=elapsed))
                if segment_start is not None:
                    local=elapsed-segment_start
                    if a.single_se and local>=.4 and ('single',stage) not in done:
                        done.add(('single',stage));button(8,1);button_release.append((elapsed+.06,8))
                    if a.se_actions:
                        for offset,key in ((3,7),(4,6),(5,8),(6,0),(7,7),(7.16,6),(7.32,7),(7.48,6)):
                            token=(stage,offset,key)
                            if local>=offset and token not in done:
                                done.add(token);button(key,1);button_release.append((elapsed+.06,key))
                    for when,key in button_release[:]:
                        if elapsed>=when:button(key,0);button_release.remove((when,key))
                    if a.recovery:
                        for target,offset,name in ((0,8,'SAVE_STATE'),(1,8,'LOAD_STATE'),(3,7,'FAST_FORWARD')):
                            token=('recover',target,name)
                            if stage==target and local>=offset and token not in done:done.add(token);command(name)
                        if stage==3 and 5<=local<5.6:command('REWIND')
                if segment_start is not None and elapsed-segment_start >= a.segment_seconds:
                    stage+=1
                    if stage==len(speeds):break
                    if speeds[stage]!=speeds[stage-1]:speed_change(speeds[stage])
                    segment_start=elapsed
                    commands.append(dict(command='SEGMENT_BEGIN',speed=speeds[stage],elapsed=elapsed))
                # NCI REWIND is a per-frame button. A 40ms resend produces
                # separate press/release episodes at 3x; keep it continuously
                # asserted by polling faster than a 179Hz emulated frame.
                time.sleep(.001 if a.recovery and segment_start is not None and stage==3 and 5<=elapsed-segment_start<5.6 else .04)
            command('QUIT')
            try:rc=proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                # Only the process created above; never another emulator.
                proc.terminate();proc.wait(timeout=5);rc='OWN_PROCESS_BOUNDED_STOP'
            cpu=cpu_seconds(proc)
    finally:
        if proc is not None and proc.poll() is None:proc.kill();proc.wait()
        if stream: stream.stop_stream();stream.close();audio.terminate()
    wall=time.perf_counter()-start
    wav_rate=rate
    if a.audible:
        capture=b''.join(parts)
        if a.compact_capture and rate!=48000:
            from scipy.signal import resample_poly
            from math import gcd
            divisor=gcd(rate,48000)
            samples=np.frombuffer(capture,dtype='<i2').reshape(-1,channels)
            capture=np.rint(np.clip(resample_poly(samples.astype(float),48000//divisor,rate//divisor,axis=0),-32768,32767)).astype('<i2').tobytes()
            wav_rate=48000
        with wave.open(str(d/'windows-loopback.wav'),'wb') as w:
            w.setnchannels(channels);w.setsampwidth(2);w.setframerate(wav_rate);w.writeframes(capture)
    text=log.read_text(errors='replace') if log.exists() else ''
    segments=[c for c in commands if c['command']=='SEGMENT_BEGIN'];rows=[]
    for i,s in enumerate(segments):
        end=segments[i+1]['elapsed'] if i+1<len(segments) else commands[-1]['elapsed']
        seen={o[1]:o for o in observations if s['elapsed']+2<o[0]<end-1}
        q=list(seen.values());measured=None;sample_rate=None
        if len(q)>1:
            first,last=q[0],q[-1];dt=(last[2]-first[2])/1e6
            measured=(last[1]-first[1])/dt/FPS;sample_rate=(last[3]-first[3])/dt
        rows.append(dict(target_speed=s['speed'],contains_recovery_actions=a.recovery,
            contains_extra_speed_toggle=a.recovery and i==3,begin=s['elapsed'],end=end,probes=q,
            measured_speed_x=measured,fixed_samples_per_second=sample_rate,
            all_steady_probes_fixed=bool(q) and all(o[4] for o in q)))
    pcm=np.frombuffer(b''.join(parts),dtype='<i2')
    report=dict(inputs=inputs,commands=commands,segments=rows,returncode=rc,wall_seconds=wall,
        start_perf=start,
        one_core_cpu_percent=cpu/wall*100,loopback_device=dev,capture_status_counts={str(s):statuses.count(s) for s in set(statuses)},
        capture_seconds=len(pcm)/channels/rate,capture_peak=int(np.max(np.abs(pcm.astype(np.int32)))) if len(pcm) else 0,
        wav_sha256=sha(d/'windows-loopback.wav') if a.audible else None,wav_rate=wav_rate if a.audible else None,
        mode=a.mode,actual_windows_capture=a.audible,
        mode_selected=('selected='+a.mode.capitalize()) in text,
        mode_saved=('mgba_fixed_audio_tempo_mode = "'+a.mode+'"') in opt.read_text(),
        frontend_muted=a.muted,video_driver=settings['video_driver'],system_volume_changed=False,
        state_load_confirmed=state_loaded(text,a.mode),
        fallback_reasons=re.findall(r'status=FALLBACK[^\n]*reason=([^\n]+)',text),
        ring_underrun_max=max([0]+list(map(int,re.findall(r'underrun=(\d+)',text)))),
        ring_overrun_max=max([0]+list(map(int,re.findall(r'overrun=(\d+)',text)))),
        b6jj_fallback_reasons=re.findall(r'NATIVE_FALLBACK reason=([^\s]+)',text),
        human_listening='HUMAN_REVIEW_REQUIRED',physical_device_underruns='NOT_INSTRUMENTED',
        host_mix_contamination='Not process-isolated; WASAPI captures the selected output endpoint mix.')
    report.update(se_actions=a.se_actions,single_se_action=a.single_se,recovery_actions=a.recovery,
        no_pcm_capture=a.no_pcm_capture,
        incremental_log=a.incremental_log,host_log_poll_seconds_total=sum(log_cost),host_log_poll_seconds_max=max(log_cost,default=0),
        b6jj_cues=re.findall(r'\[B6JJ EVENT\][^\n]*',text),
        recovery_markers=re.findall(r'\[(?:B6JJ RECOVERY|FIXED AUDIO)\][^\n]*(?:READY|LOAD|REWIND|REBIND|RECOVER)[^\n]*',text))
    (d/'capture-times.json').write_text(json.dumps(capture_times,indent=2)+'\n')
    (d/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    assert all(sha(Path(f))==h for f,h in inputs.items()),'Input changed'
    assert report['state_load_confirmed'] and report['mode_selected']
    assert not a.audible or report['capture_peak']>0
    assert len(segments)==len(speeds),'Incomplete speed schedule'
    print(json.dumps({k:report[k] for k in ('segments','capture_peak','capture_seconds','state_load_confirmed','fallback_reasons')}))

if __name__=='__main__':main()
