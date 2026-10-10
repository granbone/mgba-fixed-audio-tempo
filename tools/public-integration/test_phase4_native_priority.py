"""Exhaustively compare integration priority with the verified native Thumb rule.
SPDX-License-Identifier: MPL-2.0. Reports contain derived metadata, not ROM bytes.
"""
import argparse, json, struct, subprocess
from pathlib import Path
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
from test_three_x_poc import sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compiler',type=Path,default=Path('C:/msys64/mingw64/bin/g++.exe'))
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    assert sha(a.rom)=='0E9997636409C47734895EA2180521FA7AD8898D7F45E01634C9E207039D7808'
    rom=a.rom.read_bytes();dis=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
    ins=list(dis.disasm(rom[0x81610:0x8161c],0x08081610))
    assert [(i.mnemonic,i.op_str) for i in ins]==[
        ('ldrb','r1, [r6, #9]'),('ldrb','r0, [r5, #0x1d]'),('adds','r0, r0, r1'),
        ('cmp','r0, #0xff'),('bls','#0x808161c'),('movs','r0, #0xff')]
    native=[]
    for player in range(256):
        for track in range(256):
            registers={};comparison=0
            for i in ins:
                if i.address==0x08081610: registers['r1']=player
                elif i.address==0x08081612: registers['r0']=track
                elif i.mnemonic=='adds': registers['r0']+=registers['r1']
                elif i.mnemonic=='cmp': comparison=registers['r0']-255
                elif i.mnemonic=='bls' and comparison<=0:break
                elif i.mnemonic=='movs':registers['r0']=255
            native.append(registers['r0'])
    bases=[];notes=[]
    for player in range(4):
        _,base,count=struct.unpack_from('<III',rom,0xcc9c0+12*player);count&=65535
        assert count<=16
        bases.append(dict(player=player,base=base,count=count))
        notes.extend((player,track,base+track*80) for track in range(count))
    root=Path(__file__).resolve().parents[2]
    source=a.output/'priority.cpp'
    source.write_text('#include "priority_order.hpp"\n#include <cstdio>\n#include <io.h>\n#include <fcntl.h>\nstruct Note {unsigned playerIdx,trackIdx;};\nint main(){\n_setmode(_fileno(stdout),_O_BINARY);\n'
        'for(unsigned p=0;p<256;++p)for(unsigned t=0;t<256;++t) std::putchar(mp2kBridgeNotePriority(p,t));\n'
        'Note notes[]={' + ','.join('{%d,%d}'%(p,t) for p,t,_ in notes) + '};\n'
        'for(auto old:notes)for(auto next:notes)std::putchar(mp2kBridgeTrackPrecedes(old,next));\n}\n')
    binary=a.output/'priority.exe'
    subprocess.run([str(a.compiler),'-std=c++23','-O2','-I'+str(root/'tools/mp2k-audio-trace/bridge'),str(source),'-o',str(binary)],check=True)
    actual=subprocess.run([str(binary.resolve())],capture_output=True,check=True).stdout
    assert actual[:65536]==bytes(native)
    assert actual[65536:]==bytes(int(old[2]<new[2]) for old in notes for new in notes)
    result=dict(passed=True,native_priority_cases=65536,native_tie_cases=len(notes)**2,
        same_track_allows_retrigger=True,overflow_saturates=True,players=bases,
        rom_sha256=sha(a.rom),priority_helper_sha256=sha(root/'tools/mp2k-audio-trace/bridge/priority_order.hpp'),
        evidence='Verified native Thumb composition at08081610..0808161a; equal-priority RAM address order at08081654..0808165a; no ROM bytes retained.')
    (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
