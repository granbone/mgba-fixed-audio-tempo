"""Design obligations backed by native counterexamples; not audio validation.
SPDX-License-Identifier: MPL-2.0.
"""
import argparse,json
from pathlib import Path
from phase8_lifetime_model import LifetimeModel,Reason

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    assert not a.output.exists();cases=[]
    def check(name,condition):assert condition,name;cases.append(dict(case=name,passed=True))
    m=LifetimeModel();old=m.confirmed_start(2,0x0200f2f0,0x03005d50,0x0835781c,100,accepted=True)
    check('render accepted owner',m.render_start(old)=='APPLY_PLAY')
    check('checked native finish records source, not permission',m.observed_natural_finish(old,checked_instruction=True,terminal_tracks=True)==Reason.NATURAL_END)
    check('incomplete cancellation coverage cannot defer STOP',m.render_stop(old,Reason.NATURAL_END)=='UNKNOWN_REQUIRES_NATIVE_FALLBACK')
    check('unknown STOP never becomes natural',m.render_stop(old,Reason.UNKNOWN)=='UNKNOWN_REQUIRES_NATIVE_FALLBACK')
    check('rejected Start retains owner',m.confirmed_start(2,old.address,old.tracks,old.header,110,accepted=False)==old)
    new=m.confirmed_start(2,old.address,old.tracks,old.header,120,accepted=True)
    check('same-header accepted reuse changes generation',new.generation!=old.generation and new.header==old.header)
    check('native-ahead Start cannot invalidate still-rendered old STOP',m.render_stop(old,Reason.EXPLICIT_STOP)=='APPLY_STOP')
    check('new owner starts in audio order',m.render_start(new)=='APPLY_PLAY')
    check('old STOP cannot kill reused owner',m.render_stop(old,Reason.EXPLICIT_STOP)=='DISCARD_STALE_OWNER')
    check('old FINE cannot mark new owner finished',m.observed_natural_finish(old,checked_instruction=True,terminal_tracks=True)==Reason.UNKNOWN)
    check('track end cannot stop unrelated tracks',m.render_stop(new,Reason.TRACK_END)=='UNKNOWN_REQUIRES_VOICE_OR_TRACK_RECIPIENT')
    check('voice steal cannot stop BGM player',m.render_stop(new,Reason.VOICE_STEAL)=='UNKNOWN_REQUIRES_VOICE_OR_TRACK_RECIPIENT')
    check('explicit cancel after FINE still applies',m.render_stop(new,Reason.EXPLICIT_STOP)=='APPLY_STOP')
    other=m.confirmed_start(0,0x0200f270,0x03005670,0x08351518,130,accepted=True);m.render_start(other)
    check('cancelled SFX does not stop BGM owner',m.rendered.get(0)==other)
    m.restore();check('restore discards ongoing independent SFX',not m.rendered and not m.native and not m.finished)
    check('old session Stop cannot affect post-load owner',m.render_stop(other,Reason.EXPLICIT_STOP)=='DISCARD_OLD_SESSION')
    fresh=m.confirmed_start(0,other.address,other.tracks,other.header,140,accepted=True);m.render_start(fresh)
    check('post-restore accepted BGM can start',m.rendered[0]==fresh)
    check('song change uses the intended owner',m.render_stop(fresh,Reason.SONG_CHANGE)=='APPLY_STOP')
    try:m.confirmed_start(32,0,0,0,0,accepted=True)
    except ValueError:bounded=True
    else:bounded=False
    check('player bound enforced',bounded)
    m.serial=(1<<64)-1
    try:m.confirmed_start(0,0,0,0,0,accepted=True)
    except OverflowError:bounded=True
    else:bounded=False
    check('generation wrap requires fallback',bounded)
    m=LifetimeModel()
    for i in range(512):
        owner=m.confirmed_start(0,0x0200f270,0x03005670,0x08351518,i,accepted=True)
        m.observed_natural_finish(owner,checked_instruction=True,terminal_tracks=True)
    owner=m.confirmed_start(0,0x0200f270,0x03005670,0x08351518,513,accepted=True)
    try:m.observed_natural_finish(owner,checked_instruction=True,terminal_tracks=True)
    except OverflowError:bounded=True
    else:bounded=False
    check('unconsumed finish proofs are bounded to the event queue',bounded)
    a.output.write_text(json.dumps(dict(passed=True,cases=cases,connected_to_product=False,
        limitation='Model obligations only. No native Start-return hook, all-write provenance or voice-target bridge ABI is implemented. No SFX fix or loop cancellation certification.'),indent=2)+'\n')
    print(len(cases),'design cases; product integration NOT implemented')

if __name__=='__main__':main()
