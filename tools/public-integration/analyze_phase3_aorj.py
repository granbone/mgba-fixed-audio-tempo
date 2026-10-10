"""AORJ phase alignment and native arbitration evidence, without changing a golden.
SPDX-License-Identifier: MPL-2.0. ROM is read-only; reports contain metadata only.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import numpy as np
from scipy.signal import correlate,fftconvolve

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=a.root;folder=root/'build-phase2/live-AORJ-confirmed'
    x,y=[np.fromfile(folder/f'experimental-{s}/fixed-callback.s16le','<i2').reshape(-1,2) for s in (1,2)]
    rate=32768;rows=[]
    for t in (7,10,13,16):
        q=y[t*rate:(t+2)*rate].mean(axis=1);region=x[5*rate:23*rate].mean(axis=1)
        scores=correlate(region,q,'valid',method='fft')/np.sqrt(np.maximum(fftconvolve(region**2,np.ones(len(q)),'valid'),1)*np.dot(q,q))
        i=int(scores.argmax());begin=i+5*rate
        rows.append(dict(start_seconds=t,offset_stereo_samples=begin-t*rate,
            native_sample_correlation=float(scores[i]),stereo_byte_exact=bool(np.array_equal(x[begin:begin+2*rate],y[t*rate:(t+2)*rate]))))
    offsets={r['offset_stereo_samples'] for r in rows};span_exact=False
    if len(offsets)==1:
        offset=next(iter(offsets));span_exact=bool(np.array_equal(x[7*rate+offset:18*rate+offset],y[7*rate:18*rate]))
    rom=a.rom.read_bytes();assert sha(a.rom)=='0E9997636409C47734895EA2180521FA7AD8898D7F45E01634C9E207039D7808'
    table=0xcc9c0;tracks=[]
    for i in range(4):
        player,base,info=struct.unpack_from('<III',rom,table+12*i);count=info&0xffff
        tracks.append(dict(player=i,address=hex(player),track_base=hex(base),count=count,
            track_end=hex(base+count*0x50)))
    aorj=dict(rom_sha256=sha(a.rom),native_table_offset=hex(table),players=tracks,
        native_equal_priority_track_compare_pc='08081654..0808165a',
        native_rule='Existing track address < incoming track address rejects incoming on equal combined priority; equal addresses allow replacement.',
        ordinal_matches_native_track_order=all(int(tracks[i]['track_end'],16)<=int(tracks[i+1]['track_base'],16) for i in range(3)),
        native_priority_composition_pc='0808160e..0808161c',native_priority_rule='min(255, player.priority + track.priority)',
        bridge_priority_rule='track.priority only, inherited from pinned agbplay',
        conflict=dict(bgm_header='081448f4',bgm_player=0,bgm_song_priority=rom[0x1448f4+2],
            se_header='0816bd40',se_player=2,se_song=202,se_song_priority=rom[0x16bd40+2],
            bridge_equal_priority=0,legacy_logical_decision='Reject player2 track0 key88 behind player0 track1 key55',
            native_owner_trace='PSG channel1 trigger writes at cycles286869077..286869386 select track03005a10 (player2 track0).',
            status='CONFIRMED_NATIVE_VOICE_SELECTION_MISMATCH'))
    legacy=np.fromfile(root/'build-phase3/aorj-priority-legacy/callback.s16le','<i2').reshape(-1,2)
    probe=np.fromfile(root/'build-phase3/aorj-priority-native-priority/callback.s16le','<i2').reshape(-1,2)
    differences=np.flatnonzero(np.any(legacy!=probe,axis=1))
    aorj['private_priority_probe']=dict(first_difference_stereo_sample=int(differences[0]),
        first_difference_seconds=float(differences[0]/rate),legacy_sha256=sha(root/'build-phase3/aorj-priority-legacy/callback.s16le'),
        native_priority_sha256=sha(root/'build-phase3/aorj-priority-native-priority/callback.s16le'),
        production_bridge_replaced=False,fully_validated_fix=False)
    report=dict(phase2_2x=dict(windows=rows,continuous_7_to_18_seconds_stereo_byte_exact=span_exact,
        explanation='988 native samples = 482.421875 samples at the old 16kHz comparison rate. Independently downsampled recordings have different filter/sample-grid phase. The old threshold result is retained; additional native-rate byte evidence resolves the waveform concern.'),
        native_semantics=aorj,release_decision='HOLD_FOR_NATIVE_PRIORITY_SE_FIX_AND_REGRESSION')
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
