"""B6JJ event-aligned SE residual evidence; not a standalone SE certification.
SPDX-License-Identifier: MPL-2.0. Audio remains in private scene directories.
"""
import argparse,json,re
from pathlib import Path
import numpy as np

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scene',type=Path,required=True);p.add_argument('--background',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();rate=65536;records=[];clips={}
    for speed in (1,2,3):
        d=a.scene/str(speed);text=(d/'run.log').read_text(errors='replace')
        events=[]
        for line in text.splitlines():
            if '[B6JJ EVENT]' not in line or 'kind=SE' not in line:continue
            fields={k:int(v) for k,v in re.findall(r'(seq|gameCycle|audioSample|targetCycle|actualCycle|id)=(\d+)',line)}
            fields['late_samples']=(fields['actualCycle']-fields['targetCycle'])/256
            fields['late_ms']=fields['late_samples']/rate*1000;events.append(fields)
        assert events and len({e['seq'] for e in events})==len(events)
        x=np.fromfile(d/'callback.s16le',dtype='<i2').reshape(-1,2).astype(np.float64)
        b=np.fromfile(a.background/str(speed)/'callback.s16le',dtype='<i2').reshape(-1,2).astype(np.float64)
        n=min(len(x),len(b));residual=(x[:n]-b[:n]).mean(axis=1)
        start=events[0]['audioSample'];clip=residual[start:start+8192]
        assert len(clip)==8192;clip-=clip.mean();clips[speed]=clip
        spectrum=abs(np.fft.rfft(clip*np.hanning(len(clip))));f=np.fft.rfftfreq(len(clip),1/rate);band=(f>=40)&(f<=8000)
        records.append(dict(speed=speed,events=events,event_count=len(events),
            first_residual_peak_hz=float(f[band][np.argmax(spectrum[band])]),frequency_bin_hz=8,
            input_order_matches=all(e['id']==122 and e['seq']==i+1 for i,e in enumerate(events)),
            duration='INCONCLUSIVE',duration_reason='SE changes the shared mixer; subtracting a no-input BGM leaves continuing phase differences. Residual duration cannot certify SE lifetime.'))
    comparisons=[]
    for speed in (2,3):
        x,y=clips[1],clips[speed];sx,sy=abs(np.fft.rfft(x*np.hanning(len(x)))),abs(np.fft.rfft(y*np.hanning(len(y))))
        comparisons.append(dict(speed=speed,first_125ms_residual_correlation=float(np.dot(x,y)/max(np.linalg.norm(x)*np.linalg.norm(y),1e-20)),
            spectral_cosine=float(np.dot(sx,sy)/max(np.linalg.norm(sx)*np.linalg.norm(sy),1e-20)),
            onset_delta_samples=[b['audioSample']-a['audioSample'] for a,b in zip(records[0]['events'],records[speed-1]['events'])]))
    a.output.write_text(json.dumps(dict(records=records,comparisons=comparisons,
        scope='Three controlled B6JJ menu SE id122 triggers. Event order/count, applied lateness and 125ms mixed residual spectrum; no resampling/pitch correction. Full SE pitch/lifetime and simultaneous voices HUMAN_REVIEW_REQUIRED.'),indent=2)+'\n')
    print(json.dumps(comparisons))
if __name__=='__main__':main()
