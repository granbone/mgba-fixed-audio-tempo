# v0.3 RC technical-demo review

Local recordings made on 2026-10-09. No video is added to Git. The user adopted both finished recordings and confirmed game-footage/audio rights for publication. This is the user's confirmation, not independently obtained or certified third-party permission. Verified Release asset links are listed in README.

Both recordings use the newly built v0.3 RC from production source `75872eb6899ce266e2cae452b5e2a1154d185a46`, not the existing v0.2 distribution. OBS Studio 32.0.1 captured actual RetroArch gameplay and one Windows Desktop Audio WASAPI source. Application audio, extra desktop audio and microphones were disabled. RivaTuner was disabled. Controller input replay supplies real core button inputs; no recorded footage is played back to simulate gameplay. Video speed, audio tempo and pitch were not altered in postproduction.

| Recording | Scene | Duration | Bytes | SHA256 |
| --- | --- | ---: | ---: | --- |
| FFTA_demo_v03_review.mp4 | AFXJ battle | 55.000 s | 8,027,158 | B646B05ADE30FE8E28E799B920C32C93B992C559427212B0FE88769BC75BD1B7 |
| OrientalBlue_demo_v03_review.mp4 | AORJ walking | 55.016667 s | 5,004,606 | C5B0B7DF5654EA3509A6C0F3720D78EF7917D9672147FA231CDEB94429387057 |

Actual DLL SHA256:

- Core: `A4BD66BA6A50CFC59109971DC1937BD3675991EE5E302FD3C1554F93C114BBF1`.
- MP2K bridge: `6C3EFC3F5FEDA53052A4432322A62357925842B79C54396479FFFCE857D29A40`.
- zlib dependency: `93E9243A44C29200EEACAF9658EFE2558581770E4B11CA4B500E18E424A6E3B5`.

The library's Git-derived version string is `0.11-feature/v03-experimental-presentation-7-v0.`. Its nearest-tag ancestry is not a claim that the DLL is the v0.2 canonical binary.

## Recording and audio evidence

| Check | FFTA | Oriental Blue |
| --- | --- | --- |
| Measured frame progression, 1x / 2x / 1x | 1.0093 / 1.9993 / 1.0088 | 1.0083 / 2.0003 / 1.0099 |
| Six 2-second OBS/independent Windows-output correlations | 0.999841–0.999917 | 0.999641–0.999812 |
| WASAPI callback errors | None reported | None reported |
| Digital clipping | None detected | None detected |
| Edited audio | Original AAC copied; decoded audio SHA256 equal | Original AAC copied; decoded audio SHA256 equal |
| Video frames | All 3,299 retained | All 3,300 retained |
| Timestamp preservation | Within 0.334 ms of muxer timebase quantization | Within 0.334 ms of muxer timebase quantization |
| BGM comparison | Spectral sequence favors near-1x duration | Spectral sequence favors near-1x duration |
| Exact raw-waveform match / exact pitch | Inconclusive / not certified in cents | Inconclusive / not certified in cents |
| BGM/SE/synchronization listening | Preceding battle test: user reported no discomfort; finished video adopted | Finished video adopted by user |
| Finished 55-second video adoption | USER_ADOPTED | USER_ADOPTED |

Measured speed uses 59.7275 Hz as the normal frame-rate reference. Independent same-state 1x/2x Windows captures were compared with phase-independent STFT sequences at relative durations 0.5x, near 1x, and 2x. Near-1x fits were best in all four sampled windows, but this does not prove the correctness of every note or sound effect. The initial direct waveform comparison failed its match threshold and remains recorded as inconclusive; it was not hidden or promoted to PASS.

The preceding FFTA listening statement concerned the battle test; the current user instruction separately adopts the finished file. AORJ's ACTIVE observation alone is not a correctness pass. Caption audio labels are scoped to the selected scene; AORJ explicitly labels tempo/pitch as targets and shows the review requirement. No full playthrough or worldwide/regional compatibility claim is made, and no new database identity is promoted on this evidence alone.

## User publication authorization and rights confirmation

The user explicitly approved formal v0.3 publication, adopted both exact finished files above, and states that game-footage/audio rights have been checked. We record this confirmation without claiming independent title-specific legal clearance or a third-party license obtained by the project. Rights notices appear in README and Release notes. Original videos are unchanged. Old withdrawn recordings are excluded.

The distribution retains the exact recording RC DLL bytes. A fresh rebuild from the new clean public source is separately validated; different Git ancestry/version metadata and PE timestamps do not imply a change to the production source. No full-playthrough/all-scene claim or new compatibility promotion follows from adopting these videos.
