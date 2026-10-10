"""Isolated SE202 synthesis: frontend batch size never changes pitch/lifetime.
SPDX-License-Identifier: MPL-2.0. Synthetic bridge probe, no native quality claim.
"""
import argparse,ctypes as c,json,hashlib,wave
from pathlib import Path
from test_three_x_poc import sha

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('bridge','rom','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 inputs={str(f.resolve()):sha(f) for f in (a.bridge,a.rom)}
 assert sha(a.rom)=='0E9997636409C47734895EA2180521FA7AD8898D7F45E01634C9E207039D7808'
 raw=a.rom.read_bytes();rom=(c.c_uint8*len(raw)).from_buffer_copy(raw);lib=c.CDLL(str(a.bridge.resolve()))
 lib.mp2k_bridge_create.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32];lib.mp2k_bridge_create.restype=c.c_void_p
 lib.mp2k_bridge_render.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t]
 lib.mp2k_bridge_play.argtypes=[c.c_void_p,c.c_uint16,c.c_uint8]
 lib.mp2k_bridge_stop.argtypes=[c.c_void_p,c.c_uint16,c.c_uint8]
 lib.mp2k_bridge_destroy.argtypes=[c.c_void_p];streams={};rows=[]
 for label,batch,stop_sample in [('1x-reference',548,None),('2x-batches',274,None),('3x-batches',183,None),('explicit-stop',183,2926)]:
  handle=lib.mp2k_bridge_create(rom,len(raw),32768);assert handle
  assert lib.mp2k_bridge_play(handle,202,2)==0
  data=bytearray();position=0
  while position<32768:
   if stop_sample==position:assert lib.mp2k_bridge_stop(handle,202,2)==0
   n=min(batch,32768-position)
   if stop_sample is not None and position<stop_sample: n=min(n,stop_sample-position)
   pcm=(c.c_int16*(n*2))();assert lib.mp2k_bridge_render(handle,pcm,n)==0;data.extend(bytes(pcm));position+=n
  lib.mp2k_bridge_destroy(handle);streams[label]=bytes(data)
  path=a.output/(label+'.wav')
  with wave.open(str(path),'wb') as f:f.setparams((2,2,32768,0,'NONE','not compressed'));f.writeframes(data)
  rows.append(dict(label=label,batch_frames=batch,explicit_stop_sample=stop_sample,pcm_sha256=hashlib.sha256(data).hexdigest().upper(),wav_sha256=sha(path),wav=str(path.resolve()),seconds=1))
 assert streams['1x-reference']==streams['2x-batches']==streams['3x-batches']
 assert streams['explicit-stop']!=streams['1x-reference']
 assert streams['explicit-stop'][:2926*4]==streams['1x-reference'][:2926*4]
 result=dict(inputs=inputs,rows=rows,normal_vs_2x_3x_batch_pcm_exact=True,explicit_stop_changes_only_after_target=True,
  status='PASS_ISOLATED_SYNTHESIS',limits='Identical unresampled PCM establishes batch-independent SE pitch and duration in the bridge. Not a whole-game/native auditory equivalence proof.')
 assert all(sha(Path(f))==h for f,h in inputs.items())
 (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
