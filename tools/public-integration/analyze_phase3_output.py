"""Unretimed Windows output phrase and spectrum observations (not listening PASS).
SPDX-License-Identifier: MPL-2.0. No time stretching, pitch correction or edited originals.
"""
import argparse,json,wave
from pathlib import Path
import numpy as np
from scipy.signal import correlate,fftconvolve,resample_poly,stft

def read(p):
    with wave.open(str(p)) as w:
        assert w.getsampwidth()==2
        x=np.frombuffer(w.readframes(w.getnframes()),'<i2').reshape(-1,w.getnchannels()).mean(axis=1)/32768
        return resample_poly(x,16000,w.getframerate())

def features(x):
    f,t,z=stft(x,fs=16000,nperseg=1024,noverlap=864,boundary=None,padded=False)
    band=(f>=40)&(f<=6000)
    # Spectrum magnitude ignores oscillator phase, unlike a raw-wave hash.
    return np.sqrt(abs(z[band])).T,t

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();rows=[]
    for code in ('AAMJ','AFXJ','AORJ','B6JJ'):
        d=a.root/'build-phase3'/('live-'+code)
        ref=read(a.root/'build-phase2/human-review'/(code+'-1x-core-private.wav'))
        fast=read(d/'windows-loopback.wav');reference,times=features(ref);windows=[]
        for start in (17,20,23):
            q=fast[start*16000:(start+1)*16000];query,_=features(q)
            dots=correlate(reference,query,mode='valid',method='fft').ravel()
            energies=fftconvolve(np.sum(reference**2,axis=1),np.ones(len(query)),mode='valid')
            scores=dots/np.sqrt(np.maximum(energies,1e-20)*np.sum(query**2));i=int(np.argmax(scores))
            position=float(times[i]-.032);r=ref[round(position*16000):round(position*16000)+len(q)]
            if len(r)!=len(q):continue
            f=np.fft.rfftfreq(len(q),1/16000);band=(f>=40)&(f<=6000)
            rfft=abs(np.fft.rfft(r*np.hanning(len(r))));qfft=abs(np.fft.rfft(q*np.hanning(len(q))))
            rhz=float(f[band][rfft[band].argmax()]);qhz=float(f[band][qfft[band].argmax()])
            windows.append(dict(output_start_seconds=start,reference_start_seconds=position,
                spectral_phrase_similarity=float(scores[i]),reference_dominant_hz=rhz,
                output_dominant_hz=qhz,dominant_frequency_ratio=qhz/rhz if rhz else None,
                same_sample_scale=True))
        slope=float(np.polyfit([r['output_start_seconds'] for r in windows],[r['reference_start_seconds'] for r in windows],1)[0]) if len(windows)>1 else None
        rows.append(dict(game=code,windows=windows,phrase_time_slope=slope,
            observation='MEASUREMENT_ONLY',auditory_quality='HUMAN_REVIEW_REQUIRED'))
    report=dict(records=rows,method='Physical WASAPI endpoint mix vs loaded 1x core reference. Unretimed 1s windows; phase-insensitive STFT magnitude search. Dominant FFT bins have 1Hz resolution and are not complete note pitch certification. No new golden or waveform PASS criterion.')
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
