"""Unretimed PCM comparisons; store numbers, never copyrighted audio, in JSON.
SPDX-License-Identifier: MPL-2.0.
"""
import argparse,json,re
from pathlib import Path
import numpy as np
from scipy.signal import correlate,fftconvolve

def windows(reference,fast,rate,starts=(2,5,8,11,14,17),search=.5):
    rows=[]
    for start in starts:
        q=fast[int(start*rate):int((start+2)*rate)]
        begin=max(0,int((start-search)*rate));end=int((start+2+search)*rate)
        region=reference[begin:end]
        if len(q)!=2*rate or len(region)<len(q) or np.dot(q,q)<1e-5:continue
        dots=correlate(region,q,mode='valid',method='fft')
        energy=fftconvolve(region*region,np.ones(len(q)),mode='valid')
        scores=dots/np.sqrt(np.maximum(energy,1e-20)*np.dot(q,q));j=int(np.argmax(scores))
        x=region[j:j+len(q)]
        sx=abs(np.fft.rfft(x*np.hanning(len(x))));sq=abs(np.fft.rfft(q*np.hanning(len(q))))
        freqs=np.fft.rfftfreq(len(x),1/rate)
        band=(freqs>=40)&(freqs<=8000)
        rows.append(dict(fast_start_seconds=start,reference_start_seconds=(begin+j)/rate,
            offset_samples=begin+j-int(start*rate),correlation=float(scores[j]),
            spectral_cosine=float(np.dot(sx,sq)/max(np.linalg.norm(sx)*np.linalg.norm(sq),1e-20)),
            reference_dominant_hz=float(freqs[band][np.argmax(sx[band])]),
            fast_dominant_hz=float(freqs[band][np.argmax(sq[band])]),same_sample_scale=True))
    return rows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('scene',type=Path)
    p.add_argument('--rate',type=int,default=32768);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();raw={s:np.fromfile(a.scene/str(s)/'callback.s16le',dtype='<i2').reshape(-1,2) for s in (1,2,3)}
    mono={s:x.mean(axis=1)/32768 for s,x in raw.items()};comparisons=[]
    for speed in (2,3):
        n=min(len(raw[1]),len(raw[speed]));diff=np.flatnonzero(np.any(raw[1][:n]!=raw[speed][:n],axis=1))
        rows=windows(mono[1],mono[speed],a.rate)
        clocks=[tuple(map(float,x)) for x in re.findall(r'\[AUDIO CLOCK\] run=(\d+) rate=([\d.]+) advance=(\d+) sample=(\d+)',(a.scene/str(speed)/'run.log').read_text(errors='replace'))]
        stable=clocks[-120:];sample_ratio=None
        if len(stable)>1:
            x,y=stable[0],stable[-1]
            sample_ratio=(y[3]-x[3])/((y[0]-x[0])/y[1]*a.rate)
        comparisons.append(dict(speed=speed,sample_rate=a.rate,common_frames=n,
            exact_prefix_frames=int(diff[0]) if len(diff) else n,exact_prefix_seconds=(int(diff[0]) if len(diff) else n)/a.rate,
            first_different_sample=int(diff[0]) if len(diff) else None,different_stereo_frames=len(diff),
            windows=rows,phrase_time_slope=float(np.polyfit([r['fast_start_seconds'] for r in rows],[r['reference_start_seconds'] for r in rows],1)[0]) if len(rows)>1 else None,
            audio_clock_sample_rate_ratio=sample_ratio,
            observation='SAME_SCALE_AGREEMENT' if len(rows)>=3 and min(r['correlation'] for r in rows)>.97 else 'INCONCLUSIVE'))
    report=dict(comparisons=comparisons,method='Same initial scene, no input; six unretimed two-second windows, fixed +/-0.5s search, no pitch/time correction. Dominant spectral peaks are mixture measurements, not isolated instrument pitch. Listening/SE require review.')
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
