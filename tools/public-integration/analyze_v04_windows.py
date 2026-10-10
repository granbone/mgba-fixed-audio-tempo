"""Bounded Windows-output sanity measurements; no listening or pitch-pass claim.
SPDX-License-Identifier: MPL-2.0. Decodes existing private MP4 in memory only.
"""
import json,subprocess
from pathlib import Path
import numpy as np
def main():
 root=Path.cwd();d=root/'build-phase10/demos';manifest=json.loads((d/'manifest.json').read_text(encoding='utf8'));rows=[]
 for r in manifest['rows']:
  raw=subprocess.check_output(['C:/msys64/mingw64/bin/ffmpeg.exe','-v','error','-i',str(root/r['path']),'-vn','-f','s16le','-ac','2','-ar','48000','pipe:1'])
  a=np.frombuffer(raw,dtype='<i2').reshape(-1,2).astype(np.int32);n=len(a)//480
  rms=np.sqrt(np.mean(a[:n*480].astype(float).reshape(n,480,2)**2,axis=(1,2)))
  quiet=rms<=2;longest=0;run=0
  for v in quiet:run=run+1 if v else 0;longest=max(longest,run)
  rows.append(dict(game=r['game'],seconds=len(a)/48000,peak=int(np.max(abs(a))),clipped_stereo_frames=int(np.any(abs(a)>=32767,axis=1).sum()),minimum10ms_rms=float(rms.min()),maximum_near_zero_rms_seconds=longest*.01,quiet_threshold_int16_rms=2,
   listening='HUMAN_REVIEW_REQUIRED',pitch_and_tempo='Use retained unretimed PCM comparisons and human review; AAC/mixed-device wave differences are not a pitch failure.',host_mix='Default Windows endpoint; other applications can contaminate it. No other application is muted or modified.',aorj192ms='UNKNOWN / INCONCLUSIVE; this bounded sanity check is not source attribution'))
 (root/'build-phase10/windows-audio-sanity.json').write_text(json.dumps(dict(rows=rows,method='Decode existing Windows-output AAC to48kHz PCM in memory;10ms RMS/clipping only. No new WAV, time stretch, normalization or pitch correction.'),indent=2)+'\n',encoding='utf8');print(rows)
if __name__=='__main__':main()
