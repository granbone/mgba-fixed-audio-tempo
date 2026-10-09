# Fixed Audio architecture — v0.3-preview candidate

The libretro core selects Native, MP2K or exact B6JJ execution. The Core Option selects the trial policy independently of the runtime backend.

Experimental is the default without legacy OFF controls. It admits detected EWRAM structures after the same full static profile checks used by the accepted paths. ROM_PLAYER requires exactly one unambiguous scanner result, valid adjacent tables, SoundMode, bounded player/track/song data and successful profile installation. EWRAM additionally requires the complete independently checked hook cluster; polling alone cannot activate EWRAM audio. Live player magic, actual song/header/routing and event validity remain mandatory.

Conservative uses exact limited-test identities (CRC32, size and header code). AAMJ's known profile and B6JJ's existing stronger identity validation remain intact. The public DB uses full hash evidence and never transfers eligibility across matching titles or related regions.

| Responsibility | Source |
| --- | --- |
| Mode and legacy OFF precedence | src/platform/libretro/fixed_audio_mode.h; _loadFixedAudioMode at content load |
| Static backing and eligibility | src/gba/mp2k-profile.c; GBAMP2kProfileBuildRuntimeWithPolicy |
| Player/track and semantic guards | src/gba/mp2k-player.c, mp2k-events.c, mp2k-semantic.c |
| Ownership/fallback policy | src/gba/mp2k-ownership.c and libretro.c |
| Separate MP2K synthesis/reconstruction | tools/mp2k-audio-trace/bridge (LGPLv3) |
| Dedicated B6JJ execution/recovery | src/gba/b6jj-audio.c |
| Supported clock and output bounds | src/core/audio-clock.c and libretro.c |
| Load/Rewind continuation | retro_unserialize, _stateLoadRebindRun, B6JJ recovery |
| Static public eligibility evidence | tools/public-integration/probe_eligibility.c; production scanner/profile code |

Changing the mode requires content reload. Reset and intermediate Rewind states retain the mode chosen at load. The core never rewrites process environment variables. Explicit Disabled, legacy OFF and legacy TEMPO=0 prevent candidate output even if prototype/tone/all-detected variables are enabled. Explicit Conservative prevents the ALL environment variable from widening eligibility.

The legacy GBAMP2kProfileBuildRuntime wrapper retains ALL-variable compatibility for independent development tools. Libretro uses the explicit-policy function, so the environment cannot bypass frontend policy. No memory check is removed by either API.

Load/Rewind clear stale rings, mappings, queues and candidate PCM. Live continuation is reconstructed, never an unconditional song-head restart. The finite bridge reconstruction budget, bounded per-run work, hard corruption latch, invalid pointers/headers/events, queue and buffer capacities and native fallback remain unchanged. FFTA late positions may use native audio for about 18 seconds. A failed or timed-out reconstruction stops candidate audio.

Detected MP2K output retains the existing permissive audio ownership policy, distinct from memory validation. This can produce incorrect audio and is why eligibility is not a test pass. Known AAMJ ownership remains strict. Unknown drivers receive no invented backend.

The target is 2x. 3x stays unvalidated and falls back. GB/GBC Fixed Audio remains standalone research, without a production route.
