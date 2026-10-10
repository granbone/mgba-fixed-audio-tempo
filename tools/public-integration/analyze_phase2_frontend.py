"""Compare confirmed-loaded frontend runs using the unchanged Phase 1 window test.
SPDX-License-Identifier: MPL-2.0. Supports a separately rerun speed without
pretending that an earlier rejected frontend run loaded the requested state.
"""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from scipy.signal import correlate,fftconvolve,resample_poly

def read(folder):
    text=(folder/'frontend.log').read_text(errors='replace')
    assert '[B6JJ RECOVERY] LOAD' in text or 'STATE_LOAD_DETECTED' in text,'Unconfirmed state load'
    file=folder/'fixed-callback.s16le';rate=32768;source='PRE_MUTE_CORE_PCM'
    if (folder/'recorded-format.json').exists():
        meta=json.loads((folder/'recorded-format.json').read_text())['streams'][0]
        assert int(meta['channels'])==2
        file=folder/'recorded.s16le';rate=int(meta['sample_rate']);source='RETROARCH_RECORDING'
    if not file.exists() or file.stat().st_size==0:file=folder/'backend.s16le';rate=65536
    x=np.fromfile(file,dtype='<i2').reshape(-1,2).mean(axis=1)/32768
    return resample_poly(x,16000,rate),dict(source=source,file=str(file),sha256=hashlib.sha256(file.read_bytes()).hexdigest().upper(),rate=rate)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('reference','fast','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--seconds',type=float,default=24);a=p.parse_args()
    normal,ref=read(a.reference);fast,src=read(a.fast);rows=[]
    for start in (7,10,13,16):
        q=fast[start*16000:(start+2)*16000];region=normal[5*16000:int(a.seconds-1)*16000]
        if len(q)!=32000 or len(region)<len(q) or np.dot(q,q)<1e-5:continue
        dots=correlate(region,q,mode='valid',method='fft');energy=fftconvolve(region*region,np.ones(len(q)),mode='valid')
        scores=dots/np.sqrt(np.maximum(energy,1e-20)*np.dot(q,q));j=int(np.argmax(scores));x=region[j:j+len(q)]
        sx=abs(np.fft.rfft(x*np.hanning(len(x))));sq=abs(np.fft.rfft(q*np.hanning(len(q))))
        f=np.fft.rfftfreq(len(x),1/16000);band=(f>=40)&(f<=8000)
        rows.append(dict(fast_start_seconds=start,reference_start_seconds=5+j/16000,correlation=float(scores[j]),
            spectral_cosine=float(np.dot(sx,sq)/max(np.linalg.norm(sx)*np.linalg.norm(sq),1e-20)),
            reference_dominant_hz=float(f[band][np.argmax(sx[band])]),fast_dominant_hz=float(f[band][np.argmax(sq[band])]),same_sample_scale=True))
    report=dict(reference=ref,fast=src,windows=rows,
        observation='SAME_SCALE_AGREEMENT' if len(rows)>=3 and min(v['correlation'] for v in rows)>.97 else 'INCONCLUSIVE',
        phrase_time_slope=float(np.polyfit([v['fast_start_seconds'] for v in rows],[v['reference_start_seconds'] for v in rows],1)[0]) if len(rows)>1 else None,
        method='Phase 1 starts 7/10/13/16s; unretimed 2s windows, reference search 5s..(seconds-1), threshold >0.97 unchanged. Both loads confirmed in actual core log. Physical output/listening not certified.')
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
