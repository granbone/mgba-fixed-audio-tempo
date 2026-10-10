"""Executable design model, NOT connected to the core, queue or bridge.
SPDX-License-Identifier: MPL-2.0.

A confirmed native Start and a rendered Start live on different timelines.
Unknown cancellation coverage cannot authorize independent continuation.
"""
from dataclasses import dataclass
from enum import Enum

class Reason(str, Enum):
    NATURAL_END='NATURAL_END'
    EXPLICIT_STOP='EXPLICIT_STOP'
    VOICE_STEAL='VOICE_STEAL'
    TRACK_END='TRACK_END'
    SONG_CHANGE='SONG_CHANGE'
    STATE_RESTORE='STATE_RESTORE'
    UNKNOWN='UNKNOWN'

@dataclass(frozen=True)
class Owner:
    session: int
    generation: int
    player: int
    address: int
    tracks: int
    header: int
    start_cycle: int

class LifetimeModel:
    """Bounded owner model. It cannot manufacture native acceptance evidence."""
    def __init__(self):
        self.session=1
        self.serial=0
        self.native={}
        self.rendered={}
        self.finished=set()

    def confirmed_start(self, player, address, tracks, header, cycle, *, accepted):
        if not 0<=player<32:raise ValueError('player bound')
        if not accepted:return self.native.get(player)
        if self.serial==(1<<64)-1:raise OverflowError('generation exhausted: fallback required')
        self.serial+=1
        owner=Owner(self.session,self.serial,player,address,tracks,header,cycle)
        self.native[player]=owner
        return owner

    def observed_natural_finish(self, owner, *, checked_instruction, terminal_tracks):
        if self.native.get(owner.player)!=owner or not checked_instruction or not terminal_tracks:
            return Reason.UNKNOWN
        if owner not in self.finished and len(self.finished)>=512:
            raise OverflowError('proof queue exhausted: fallback required')
        self.finished.add(owner)
        return Reason.NATURAL_END

    def render_start(self, owner):
        if owner.session!=self.session:return 'DISCARD_OLD_SESSION'
        previous=self.rendered.get(owner.player)
        if previous and owner.generation<=previous.generation:return 'UNKNOWN_OUT_OF_ORDER_START'
        self.rendered[owner.player]=owner
        return 'APPLY_PLAY'

    def render_stop(self, owner, reason, *, cancellation_coverage_proven=False):
        if owner is None or reason==Reason.UNKNOWN:return 'UNKNOWN_REQUIRES_NATIVE_FALLBACK'
        if owner.session!=self.session:return 'DISCARD_OLD_SESSION'
        if self.rendered.get(owner.player)!=owner:return 'DISCARD_STALE_OWNER'
        if reason==Reason.NATURAL_END:
            if owner not in self.finished or not cancellation_coverage_proven:
                return 'UNKNOWN_REQUIRES_NATIVE_FALLBACK'
            self.finished.discard(owner)
            return 'ALLOW_INDEPENDENT_FINE'
        if reason in (Reason.VOICE_STEAL,Reason.TRACK_END):
            return 'UNKNOWN_REQUIRES_VOICE_OR_TRACK_RECIPIENT'  # never stop the entire player
        if reason==Reason.STATE_RESTORE:
            return 'UNKNOWN_REQUIRES_SESSION_RESET'
        self.rendered.pop(owner.player)
        self.finished.discard(owner)
        return 'APPLY_STOP'

    def restore(self):
        self.session+=1
        self.native.clear();self.rendered.clear();self.finished.clear()
