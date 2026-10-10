"""Locate PCM divergence using private callback-index and core diagnostic logs.
SPDX-License-Identifier: MPL-2.0. No ROM or audio bytes are written to JSON.
"""
import argparse,json,re,hashlib
from pathlib import Path
import numpy as np

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def compare_pcm(left,right,rate=32768):
    x=np.fromfile(left/'callback.s16le',dtype='<i2').reshape(-1,2)
    y=np.fromfile(right/'callback.s16le',dtype='<i2').reshape(-1,2)
    n=min(len(x),len(y));diff=np.flatnonzero(np.any(x[:n]!=y[:n],axis=1))
    result=dict(left=str(left),right=str(right),left_sha256=sha(left/'callback.s16le'),right_sha256=sha(right/'callback.s16le'),
        left_frames=len(x),right_frames=len(y),byte_exact=len(x)==len(y) and len(diff)==0,
        different_stereo_frames=len(diff),sample_rate=rate)
    if len(diff):
        first=int(diff[0]);last=int(diff[-1]);result.update(first_sample=first,last_sample=last,first_seconds=first/rate,
            max_abs_s16=int(abs(x[:n].astype(np.int32)-y[:n]).max()))
        contexts=[]
        for d in (left,right):
            text=(d/'run.log').read_text(errors='replace');lines=text.splitlines()
            callback=re.findall(r'\[HOST CALLBACK\] frame=(\d+) byteOffset=(\d+) samples=(\d+)',text)
            containing=next(((int(f),int(o)//4,int(c)) for f,o,c in callback if int(o)//4<=first<int(o)//4+int(c)),None)
            events=[s for s in lines if '[MP2K SEMANTIC]' in s or '[MP2K FIRST TICK]' in s]
            near=[s for s in events if (m:=re.search(r'audioSample=(\d+)',s)) and abs(int(m[1])-first)<24000]
            clock=[];diagnostic=[]
            if containing:
                run=containing[0]+1
                clock=[s for s in lines if '[AUDIO CLOCK]' in s and f'run={run} ' in s]
                diagnostic=[s for s in lines if '[FIXED AUDIO DIAG]' in s and f'run={run} ' in s]
            contexts.append(dict(directory=str(d),callback=containing,clock=clock,transport=diagnostic,near_events=near))
        result['first_difference_context']=contexts
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('left',type=Path);p.add_argument('right',type=Path)
    p.add_argument('--rate',type=int,default=32768);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    r=compare_pcm(a.left,a.right,a.rate);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
