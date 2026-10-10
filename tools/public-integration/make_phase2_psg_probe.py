"""Prepare an isolated diagnostic bridge; never modify the pinned agbplay source.
SPDX-License-Identifier: MPL-2.0. Build commands are documented in THREE_X_PHASE2.
"""
import argparse, shutil, subprocess
from pathlib import Path

BASE = '0acbcf2d74ac3a90e6422cf3d546be1bacba57f3'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--agbplay-source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    repo=Path(__file__).resolve().parents[2]
    # Pure source files only, no external repository history or private assets.
    for name in ('src','CMakeLists.txt','LICENSE','README.md'):
        src=a.agbplay_source/name
        if src.is_dir():shutil.copytree(src,a.output/'agbplay'/name)
        elif src.exists():
            (a.output/'agbplay').mkdir(exist_ok=True)
            shutil.copy2(src,a.output/'agbplay'/name)
    bridge=a.output/'bridge';bridge.mkdir()
    for name in ('CMakeLists.txt','bridge.cpp','bridge.h','LICENSE','COPYING.GPL-3.0.txt'):
        content=subprocess.run(['git','show',BASE+':tools/mp2k-audio-trace/bridge/'+name],cwd=repo,
                               capture_output=True,check=True).stdout
        (bridge/name).write_bytes(content)
    reader=a.output/'agbplay/src/agbplay/SequenceReader.cpp';s=reader.read_text()
    needle='    // prepare cgb polyphony suppression\n'
    assert s.count(needle)==1
    assert s.count('channels.front().track < &trk')==s.count('chn.track < &trk')==1
    diagnostic=r'''    auto phase2Priority = [&](const auto &channel, const auto &playing) {
        const bool address = channel.track < &trk;
        const bool logical = playing.playerIdx < note.playerIdx ||
            (playing.playerIdx == note.playerIdx && playing.trackIdx < note.trackIdx);
        const char* policy = std::getenv("MGBA_PHASE2_PSG_ORDER");
        const bool chosen = policy && policy[0]=='L' ? logical :
            policy && policy[0]=='R' ? !logical : address;
        if (const char* path = std::getenv("MGBA_PHASE2_PSG_TRACE")) {
            if (FILE* f = std::fopen(path,"a")) {
                std::fprintf(f,"tick=%zu header=%zu oldPlayer=%u oldTrack=%u oldKey=%u newPlayer=%u newTrack=%u newKey=%u priority=%u oldPtr=%p newPtr=%p addressReject=%u logicalReject=%u chosenReject=%u\n",
                    player.tickCount,player.songHeaderPos,playing.playerIdx,playing.trackIdx,playing.midiKeyPitch,
                    note.playerIdx,note.trackIdx,note.midiKeyPitch,note.priority,(void*)channel.track,(void*)&trk,address,logical,chosen);
                std::fclose(f);
            }
        }
        return chosen;
    };
'''
    s='#include <cstdio>\n#include <cstdlib>\n'+s
    s=s.replace(needle,needle+diagnostic).replace('channels.front().track < &trk','phase2Priority(channels.front(), playing_note)').replace('chn.track < &trk','phase2Priority(chn, playing_note)')
    reader.write_text(s)
    print('Prepared isolated probe:',a.output)
    print('ADDRESS preserves the historical comparator; LOGICAL selects stable player/track order.')
    print('REVERSE is a diagnostic complement, including equality, not a production policy.')
if __name__=='__main__':main()
