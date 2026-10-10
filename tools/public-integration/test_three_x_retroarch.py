"""Background Windows RetroArch/XAudio2 3x measurements. SPDX-License-Identifier: MPL-2.0.

Counts completed core emulation frames against a monotonic frontend clock.
Null video and muted audio are defaults. Config/saves/states are fully isolated.
NCI command reference: https://docs.libretro.com/development/retroarch/network-control-interface/
"""
import argparse, hashlib, json, os, re, shutil, socket, struct, subprocess, time, wave
from pathlib import Path
import numpy as np
from scipy.signal import correlate, fftconvolve, resample_poly
from test_v03_rc import cpu_seconds, FPS

def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest().upper()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('retroarch','core','rom','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--replay',type=Path,help='Read-only same-scene BSV copied into private output')
    p.add_argument('--initial-state',type=Path,help='Read-only rawstate or BSV state; isolated explicit LOAD_STATE bypasses the BSV equal-size restriction')
    p.add_argument('--loopback',action='store_true')
    p.add_argument('--audible',action='store_true',help='Requires explicit user authorization; default stays muted')
    p.add_argument('--video-driver',default='null',help='Default null creates no game display')
    p.add_argument('--trace-runs',action='store_true',help='Diagnostic per-run logging; adds observer overhead')
    p.add_argument('--backend-events',action='store_true',help='Trace B6JJ cue timing into the private frontend log')
    p.add_argument('--record-audio',action='store_true',help='Private lossless frontend recording; adds encoder CPU cost')
    p.add_argument('--seconds',type=float,default=20)
    p.add_argument('--modes',default='experimental')
    p.add_argument('--speeds',default='1,2,3')
    p.add_argument('--port',type=int,default=56573)
    a=p.parse_args();assert not a.loopback or a.audible,'Muted background tests cannot validate physical loopback'
    assert not (a.replay and a.initial_state),'Choose replay or explicit initial state'
    a.output.mkdir(parents=True,exist_ok=False)
    before=sha(a.rom); replay_before=sha(a.replay) if a.replay else None
    state_before=sha(a.initial_state) if a.initial_state else None
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as check:check.bind(('127.0.0.1',a.port))
    def command(c):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.sendto(c.encode()+b'\n',('127.0.0.1',a.port))
    rows=[]
    for mode in a.modes.split(','):
        for speed in map(int,a.speeds.split(',')):
            d=(a.output/(mode+'-'+str(speed))).resolve();d.mkdir()
            for n in ('saves','states','system'): (d/n).mkdir()
            if a.initial_state:
                raw=a.initial_state.read_bytes()
                if a.initial_state.suffix=='.bsv':
                    size=struct.unpack_from('<I',raw,12)[0];assert 16+size<=len(raw)
                    raw=raw[16:16+size]
                (d/'states'/(a.rom.stem+'.state')).write_bytes(raw)
            opt=d/'core-options.cfg';opt.write_text('mgba_fixed_audio_tempo_mode = "'+mode+'"\n')
            # The isolated info directory must advertise the actual core's
            # existing savestate capability; otherwise RA 1.12 rejects LOAD_STATE.
            (d/(a.core.stem+'.info')).write_text('display_name = "mGBA Fixed Audio Tempo (private test)"\ncorename = "mGBA"\nsupported_extensions = "gb|gbc|gba"\nsavestate = "true"\n')
            settings=dict(video_driver=a.video_driver,menu_driver='rgui',audio_driver='xaudio',audio_out_rate='48000',
                audio_latency='64',audio_mute_enable='false' if a.audible else 'true',audio_fastforward_mute='false',audio_sync='true',
                audio_rate_control='true',video_vsync='false',video_fullscreen='false',video_windowed_fullscreen='false',
                pause_nonactive='false',config_save_on_exit='false',global_core_options='true',core_options_path=opt.as_posix(),
                system_directory=(d/'system').as_posix(),savefile_directory=(d/'saves').as_posix(),
                savestate_directory=(d/'states').as_posix(),rgui_config_directory=d.as_posix(),libretro_info_path=d.as_posix(),
                libretro_directory=a.core.resolve().parent.as_posix(),
                history_list_enable='false',content_runtime_log='false',content_runtime_log_aggregate='false',
                auto_overrides_enable='false',auto_remaps_enable='false',cheevos_enable='false',
                savestate_auto_load='false',savestate_auto_save='false',state_slot='0',network_cmd_enable='true',
                network_cmd_port=str(a.port),network_cmd_bind_address='127.0.0.1',fastforward_ratio=str(float(speed)),
                video_refresh_rate=str(FPS),log_verbosity='true',libretro_log_level='1',video_shader_enable='false',
                video_windowed_width='720',video_windowed_height='480',input_driver='sdl2',input_joypad_driver='xinput',
                ui_companion_start_on_boot='false')
            settings['quit_press_twice']='false'
            cfg=d/'retroarch.cfg';cfg.write_text(''.join(k+' = "'+v+'"\n' for k,v in settings.items()))
            env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
            env.update(MGBA_FIXED_AUDIO_WALL_PROBE='1',
                MGBA_FIXED_AUDIO_OUTPUT_PATH=str(d/'fixed-callback.s16le'),MGBA_MP2K_AUDIO_TRACE='1',
                MGBA_MP2K_AUDIO_TRACE_PATH=str(d/'backend.s16le'))
            if a.trace_runs:env['MGBA_FIXED_AUDIO_RUN_TRACE']='1'
            if a.backend_events:env['MGBA_B6JJ_EVENT_TRACE']='1'
            log=d/'frontend.log'
            # --max-frames is a bounded independent exit even when this RA
            # build does not honor the NCI QUIT command.
            frame_limit=int(FPS*(3+max(1,a.seconds-3)*speed))
            args=[str(a.retroarch),'--config',str(cfg),'--verbose','--log-file',str(log),
                  '--max-frames='+str(frame_limit),'-L',str(a.core),str(a.rom)]
            if a.record_audio:
                recording_cfg=d/'recording.cfg'
                recording_cfg.write_text('vcodec = "ffv1"\nacodec = "pcm_s16le"\npix_fmt = "bgr0"\nthreads = "2"\nformat = "matroska"\n')
                args[1:1]=['--record='+str(d/'pre-mute.mkv'),'--recordconfig',str(recording_cfg)]
            if a.replay:
                replay=d/'scene.bsv';shutil.copy2(a.replay,replay);args.insert(1,'--bsvplay='+str(replay))
            parts=[];statuses=[];stream=None;audio=None
            if a.loopback:
                import pyaudiowpatch as pa
                audio=pa.PyAudio();dev=audio.get_default_wasapi_loopback()
                rate=int(dev['defaultSampleRate']);channels=int(dev['maxInputChannels'])
                def cb(data,count,info,status):parts.append(data);statuses.append(status);return None,pa.paContinue
                stream=audio.open(format=pa.paInt16,channels=channels,rate=rate,input=True,
                    input_device_index=dev['index'],frames_per_buffer=1920,stream_callback=cb)
            startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
            start=time.perf_counter();toggle=None;last_state_command=-1.;state_load_seen=False
            recording_status='NOT_REQUESTED'
            try:
                with (d/'process.log').open('wb') as capture:
                    proc=subprocess.Popen(args,cwd=d,env=env,stdout=capture,stderr=subprocess.STDOUT,startupinfo=startup)
                    while time.perf_counter()-start<a.seconds and proc.poll() is None:
                        elapsed=time.perf_counter()-start
                        if a.initial_state:
                            current_log=log.read_text(errors='replace') if log.exists() else ''
                            state_load_seen=('[B6JJ RECOVERY] LOAD' in current_log or 'STATE_LOAD_DETECTED' in current_log)
                            # Wait for this isolated frontend's command socket.
                            # Retry an unacknowledged UDP request, never an
                            # already observed load/reconstruction.
                            if not state_load_seen and '[NetCMD]' in current_log and elapsed>1 and elapsed-last_state_command>1:
                                command('LOAD_STATE');last_state_command=elapsed
                        if speed>1 and toggle is None and elapsed>3 and (not a.initial_state or state_load_seen):
                            command('FAST_FORWARD');toggle=elapsed
                        time.sleep(.05)
                    command('QUIT')
                    try:rc=proc.wait(timeout=15)
                    except subprocess.TimeoutExpired:proc.kill();proc.wait();rc='TIMEOUT'
                    cpu=cpu_seconds(proc)
            finally:
                if stream:stream.stop_stream();stream.close();audio.terminate()
            wall=time.perf_counter()-start
            if a.record_audio and (d/'pre-mute.mkv').exists():
                recording_status='DECODE_FAILED'
                with (d/'decode.log').open('wb') as decode_log:
                    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(d/'pre-mute.mkv'),'-vn',
                        '-acodec','pcm_s16le','-f','s16le',str(d/'recorded.s16le')],stdout=decode_log,stderr=decode_log,timeout=60)
                if decoded.returncode==0:
                    meta=subprocess.run(['ffprobe','-v','error','-select_streams','a:0','-show_entries',
                        'stream=sample_rate,channels','-of','json',str(d/'pre-mute.mkv')],capture_output=True,check=True,timeout=20)
                    (d/'recorded-format.json').write_bytes(meta.stdout)
                    streams=json.loads(meta.stdout).get('streams',[])
                    recording_status='DECODED_STEREO_PCM' if streams and int(streams[0].get('channels',0))==2 else 'NO_STEREO_AUDIO'
            elif a.record_audio:recording_status='NO_RECORDING_FILE'
            if a.loopback:
                with wave.open(str(d/'windows-loopback.wav'),'wb') as w:
                    w.setnchannels(channels);w.setsampwidth(2);w.setframerate(rate);w.writeframes(b''.join(parts))
            text=log.read_text(errors='replace') if log.exists() else ''
            saved_option=opt.read_text(errors='replace')
            mode_saved=('mgba_fixed_audio_tempo_mode = "'+mode+'"') in saved_option
            mode_selected=('selected='+mode.capitalize()) in text
            # RetroArch 1.12 can accept a BSV but skip its state when fresh
            # serialize_size differs (e.g. save memory detected after boot).
            # Never label such a boot run as the requested loaded scene.
            replay_state_skipped='serializer' in text.lower() or 'シリアライザ' in text
            initial_state_loaded=bool('[B6JJ RECOVERY] LOAD' in text or 'STATE_LOAD_DETECTED' in text)
            probes=[tuple(map(int,x)) for x in re.findall(r'\[FIXED AUDIO PROBE\] frames=(\d+) us=(\d+) fixedSamples=(\d+) fixed=(\d)',text)]
            # Exclude boot/normal lead-in plus two seconds after FF. Last probe
            # is before QUIT. Intervals consist of completed emulated frames.
            steady=[]
            if probes:
                last=probes[-1]
                steady=[v for v in probes if 2_000_000<last[1]-v[1]<min(10,a.seconds-7)*1e6]
                if steady: steady.append(last)
            actual=None;samples_per_second=None
            if len(steady)>1:
                x,y=steady[0],steady[-1];seconds=(y[1]-x[1])/1e6
                actual=(y[0]-x[0])/seconds/FPS;samples_per_second=(y[2]-x[2])/seconds
            profile=[dict((k,int(v)) for k,v in re.findall(r'(frames|transportUs|gameUs|audioUs)=(\d+)',line))
                     for line in text.splitlines() if '[FIXED AUDIO PROFILE]' in line]
            stage_delta={}
            if len(profile)>1:
                stage_delta={k:(profile[-1][k]-profile[0][k])/(profile[-1]['frames']-profile[0]['frames'])
                             for k in ('transportUs','gameUs','audioUs')}
            row=dict(mode=mode,target_speed=speed,returncode=rc,wall_seconds=round(wall,4),
                cpu_seconds=round(cpu,4) if cpu is not None else None,
                one_core_cpu_percent=round(cpu/wall*100,3) if cpu is not None else None,
                measured_speed_x=round(actual,6) if actual else None,
                emulated_fps=round(actual*FPS,4) if actual else None,
                speed_attainment_percent=round(actual/speed*100,4) if actual else None,
                # A below-target measurement is not a successful 3x run.
                # Record raw rates; positive clock-boundary jitter is bounded.
                speed_test='PASS' if rc==0 and actual and speed <= actual <= speed*1.001 else 'FAIL',
                fixed_samples_per_second=round(samples_per_second,4) if samples_per_second is not None else None,
                fixed_audio_observed='FIXED AUDIO ACTIVE' in text or '[B6JJ RUN]' in text or
                    '[B6JJ AUDIO] backend=FIXED_AUDIO_BACKEND_B6JJ' in text,
                all_steady_probes_fixed=bool(steady) and all(v[3] for v in steady),steady_probes=steady,
                profile_microseconds_per_frame=stage_delta,
                fallback_reasons=sorted(set(re.findall(r'(?:SAFE FALLBACK reason=|NATIVE_FALLBACK reason=)([^\n]+)',text))),
                underrun=max([int(x) for x in re.findall(r'\bunderrun(?:_count)?=(\d+)',text)] or [0]),
                overrun=max([int(x) for x in re.findall(r'\boverrun(?:_count)?=(\d+)',text)] or [0]),
                loopback_statuses=sorted(set(statuses)),physical_device_underruns='NOT_INSTRUMENTED',
                video_driver=a.video_driver,muted=not a.audible,frontend_frame_limit=frame_limit,
                per_run_logging=a.trace_runs,libretro_log_level=1,
                frontend_recording=a.record_audio,
                recording_status=recording_status,
                mode_saved=mode_saved,mode_selected=mode_selected,
                replay_state_skipped=replay_state_skipped,initial_state_loaded=initial_state_loaded,
                bgm='HUMAN_REVIEW_REQUIRED',se='HUMAN_REVIEW_REQUIRED')
            rows.append(row);print(mode,speed,json.dumps(row),flush=True)
            (a.output/'results.json').write_text(json.dumps(dict(records=rows,rom_sha256=before,core_sha256=sha(a.core)),indent=2)+'\n')
    matches=[]
    if (a.replay or a.initial_state) and '1' in a.speeds.split(',') and 'experimental' in a.modes.split(','):
        audio_sources={}
        def read(speed):
            folder=a.output/('experimental-'+str(speed))
            if a.loopback:
                with wave.open(str(folder/'windows-loopback.wav')) as w:
                    x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').reshape(-1,w.getnchannels()).mean(axis=1)/32768
                    return resample_poly(x,16000,w.getframerate())
            file=folder/'fixed-callback.s16le'
            rate=32768
            audio_sources[speed]='PRE_MUTE_CORE_PCM'
            if (folder/'recorded-format.json').exists():
                streams=json.loads((folder/'recorded-format.json').read_text()).get('streams',[])
                if streams and int(streams[0].get('channels',0))==2:
                    file=folder/'recorded.s16le';rate=int(streams[0]['sample_rate'])
                    audio_sources[speed]='RETROARCH_RECORDING'
            if not file.exists() or file.stat().st_size==0:file=folder/'backend.s16le';rate=65536
            if not file.exists():audio_sources[speed]='NO_PCM';return np.empty(0)
            x=np.fromfile(file,dtype='<i2').reshape(-1,2).mean(axis=1)/32768
            return resample_poly(x,16000,rate)
        normal=read(1)
        for speed in (s for s in (2,3) if str(s) in a.speeds.split(',')):
            fast=read(speed);windows=[]
            for start in (7,10,13,16):
                q=fast[start*16000:(start+2)*16000];region=normal[5*16000:int(a.seconds-1)*16000]
                if len(q)!=32000 or len(region)<len(q) or np.dot(q,q)<1e-5:continue
                dots=correlate(region,q,mode='valid',method='fft')
                energy=fftconvolve(region*region,np.ones(len(q)),mode='valid')
                scores=dots/np.sqrt(np.maximum(energy,1e-20)*np.dot(q,q));j=int(np.argmax(scores))
                x=region[j:j+len(q)]
                sx=abs(np.fft.rfft(x*np.hanning(len(x))));sq=abs(np.fft.rfft(q*np.hanning(len(q))))
                windows.append(dict(fast_start_seconds=start,reference_start_seconds=5+j/16000,
                    correlation=float(scores[j]),same_sample_scale=True,
                    spectral_cosine=float(np.dot(sx,sq)/max(np.linalg.norm(sx)*np.linalg.norm(sq),1e-20))))
            observed='SAME_SCALE_AGREEMENT' if len(windows)>=3 and min(v['correlation'] for v in windows)>.97 else 'INCONCLUSIVE'
            slope=float(np.polyfit([v['fast_start_seconds'] for v in windows],
                                  [v['reference_start_seconds'] for v in windows],1)[0]) if len(windows)>1 else None
            matches.append(dict(speed=speed,windows=windows,automated_observation=observed,
                phrase_time_slope=slope,source='WASAPI_LOOPBACK' if a.loopback else audio_sources[speed],
                reference_source='WASAPI_LOOPBACK' if a.loopback else audio_sources[1],
                tempo_pitch_scope='Same loaded scene, no input, unretimed two-second windows. Human listening and SE certification remain required.'))
    assert sha(a.rom)==before
    if a.replay:assert sha(a.replay)==replay_before
    if a.initial_state:assert sha(a.initial_state)==state_before
    (a.output/'results.json').write_text(json.dumps(dict(records=rows,comparisons=matches,rom_sha256=before,
        core_sha256=sha(a.core),method='Real Windows x64 RetroArch/XAudio2. Default: no video display, muted output; pre-mute PCM files only. Completed-frame timestamps exclude startup. No physical output/listening PASS.'),indent=2)+'\n')
    assert all(r['returncode']==0 and r['mode_selected'] and r['mode_saved'] for r in rows), 'Frontend/mode check failed'
    if a.replay:assert all(not r['replay_state_skipped'] for r in rows),'BSV state was skipped; use --initial-state'
    if a.initial_state:assert all(r['initial_state_loaded'] for r in rows),'Requested state load was not observed'

if __name__=='__main__':main()
