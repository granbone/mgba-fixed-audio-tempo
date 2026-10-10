// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <cstdint>

// MP2K snapshots the saturated player + track priority into each new voice.
// Track PRIO alone discards the song header priority (and can suppress SFX).
constexpr uint8_t mp2kBridgeNotePriority(uint8_t player, uint8_t track) {
    const unsigned combined = unsigned(player) + unsigned(track);
    return static_cast<uint8_t>(combined > 255u ? 255u : combined);
}

// Native player/track identities have a stable order. Heap addresses of
// tracks from separate vectors do not, especially across cores and rebinds.
template<class Note>
constexpr bool mp2kBridgeTrackPrecedes(const Note& playing, const Note& incoming) {
    return playing.playerIdx < incoming.playerIdx ||
        (playing.playerIdx == incoming.playerIdx && playing.trackIdx < incoming.trackIdx);
}
