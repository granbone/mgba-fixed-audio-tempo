"""Prepare a private diagnostic of native combined note priority.
SPDX-License-Identifier: MPL-2.0. This is NOT a production fix or new golden.
Uses the Phase 2 LOGICAL/ADDRESS probe without disabling any core guard.
"""
import argparse, subprocess, sys
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--agbplay-source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    subprocess.run([sys.executable,str(Path(__file__).with_name('make_phase2_psg_probe.py')),
        '--agbplay-source',str(a.agbplay_source),'--output',str(a.output)],check=True)
    reader=a.output/'agbplay/src/agbplay/SequenceReader.cpp'
    s=reader.read_text();needle='    note.priority = trk.priority;'
    assert s.count(needle)==1
    s=s.replace(needle,needle+'''\n    // Diagnostic only: native MP2K saturates player + track priority.
    if (std::getenv("MGBA_PHASE3_NATIVE_PRIORITY"))
        note.priority = static_cast<uint8_t>(std::min<unsigned>(255u,
            unsigned(player.priority) + unsigned(trk.priority)));
''')
    reader.write_text(s)
    print('Private diagnostic; do not replace the validated runtime bridge.')

if __name__=='__main__':main()
