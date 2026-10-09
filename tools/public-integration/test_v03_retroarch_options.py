"""Real RetroArch mode persistence/restart in isolated dirs. SPDX-License-Identifier: MPL-2.0"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('retroarch','core','rom','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 before=hashlib.sha256(a.rom.read_bytes()).hexdigest()
 env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
 cases=[]
 for mode in ('default','experimental','conservative','disabled'):
  d=a.output/mode;d.mkdir();(d/'saves').mkdir();(d/'states').mkdir()
  options=d/'core-options.cfg'
  if mode!='default':options.write_text('mgba_fixed_audio_tempo_mode = "'+mode+'"\n',encoding='utf8')
  cfg=d/'retroarch.cfg'
  settings={'video_driver':'null','menu_driver':'null','input_driver':'null','audio_driver':'xaudio',
   'audio_mute_enable':'true','audio_fastforward_mute':'false','pause_nonactive':'false','config_save_on_exit':'false',
   'global_core_options':'true','core_options_path':options.as_posix(),'rgui_config_directory':d.as_posix(),
   'system_directory':(d/'saves').as_posix(),'savefile_directory':(d/'saves').as_posix(),'savestate_directory':(d/'states').as_posix(),
   'libretro_info_path':d.as_posix(),'playlist_directory':d.as_posix(),'runtime_log_directory':d.as_posix(),
   'cache_directory':d.as_posix(),'log_dir':d.as_posix(),'history_list_enable':'false','content_runtime_log':'false',
   'content_runtime_log_aggregate':'false','savestate_auto_save':'false','savestate_auto_load':'false',
   'auto_overrides_enable':'false','auto_remaps_enable':'false','cheevos_enable':'false','log_verbosity':'true','libretro_log_level':'0'}
  cfg.write_text(''.join(k+' = "'+v+'"\n' for k,v in settings.items()),encoding='utf8')
  for restart in (1,2):
   log=d/('restart-'+str(restart)+'.log')
   result=subprocess.run([str(a.retroarch),'--config',str(cfg),'--verbose','--log-file',str(log),
    '--max-frames=120','-L',str(a.core),str(a.rom)],cwd=d,env=env,capture_output=True,timeout=60)
   text=log.read_text(errors='replace')
   expected='experimental' if mode=='default' else mode
   saved=options.read_text(errors='replace')
   assert result.returncode==0,(mode,restart,'frontend failed')
   assert 'mgba_fixed_audio_tempo_mode = "'+expected+'"' in saved,(mode,restart,'saved option mismatch')
   selected=expected.capitalize()
   assert 'selected='+selected in text,(mode,restart,'core mode mismatch')
   if expected=='disabled':assert 'FIXED AUDIO ACTIVE' not in text and '[AUDIO CLOCK] enabled' not in text
   else:assert 'FIXED AUDIO ACTIVE' in text,(mode,restart,'backend inactive')
   cases.append(dict(mode=mode,restart=restart,saved=expected,passed=True))
   print(mode,restart,'PASS',flush=True)
 assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==before
 (a.output/'results.json').write_text(json.dumps(dict(passed=True,cases=cases,rom_read_only=True,saves_and_states_isolated=True),indent=2)+'\n')
if __name__=='__main__':main()
