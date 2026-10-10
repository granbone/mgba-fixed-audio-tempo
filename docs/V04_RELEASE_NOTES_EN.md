# mGBA Fixed Audio Tempo v0.4-preview

Windows x64 prerelease — BGM-focused, with known limitations.

## 2x / 3x gameplay, normal BGM tempo and pitch

Supported GBA BGM uses an Independent Audio Clock and event-driven synthesis while gameplay runs at 2x or 3x. No audio time-stretch or pitch correction is used. MP2K ROM_PLAYER/EWRAM_PLAYER paths remain experimental; B6JJ uses a separate backend.

**Use Experimental for 3x Fixed Audio.** Conservative retains its tested 1x/2x paths and uses native audio at 3x. Disabled always uses normal mGBA audio. Reload content after changing modes. Unknown/Unlimited frontend rates and unsafe runtime structures use native fallback; fallback does not maintain fixed tempo during fast-forward.

## Compatibility and validation

The [Compatibility Database](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/COMPATIBILITY.md) contains **3,075 exact public DAT identities**, **601 Experimental trial identities**, and **20 BGM-only confirmations in limited scenes** (ROM_PLAYER 10, EWRAM_PLAYER 9, UNKNOWN candidate 1). These 20 results are separate from general Fixed Audio status and SE verification, and are not whole-game guarantees. Regions and revisions do not inherit results.

Final preparation recorded **48/48 PCM/event, 16/16 scene comparisons, 12/12 legacy suites, and 36/36 representative 3x conditions**. The 48-condition reference is the corrected bridge; the 16 comparisons separately include eight original-v0.3-package comparisons and eight common-corrected-bridge comparisons. Direct GBA internal frameCounter measurements in actual RetroArch were **1.9916–2.0010x at target 2x** and **2.9683–3.0013x at target 3x**. These are tested-scene results, not a universal exact 3.000x guarantee. See the [final audit](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/docs/V04_FINAL_RELEASE_AUDIT.md).

## Demonstrations — updated video links

The public demonstration links were updated after the v0.4-preview release to new **RTSS-free 1080p/60fps** captures with large GAME SPEED and BGM TEMPO overlays and 0.3-second emphasis when the game-speed mode changes:

- [Oriental Blue — continuous walking (24.87 seconds)](https://youtu.be/kqMENTHstSE)
- [Final Fantasy Tactics Advance — active gameplay (34.32 seconds)](https://youtu.be/8lcoH3Lm_bE)

Both show real RetroArch gameplay with Windows audio and **1x→2x→3x twice**, without artificial video speed-up or time-stretch/pitch correction. Automated frame/overlay/audio-comparison checks passed; **human listening review of the newly recorded BGM, SE and A/V sync is still pending**. The original user-approved launch videos and their recording-specific hashes remain recorded in the [historical launch manifest](V04_DEMO_MANIFEST.json). This post-release video-link update does not change the binaries, distribution ZIP, compatibility data or tag. No MP4 files are included in Git or the ZIP.
## Known limits and safety

- Some finite SE end early at 2x/3x. BGM is the priority; full SE lifetime/loop/cancel/pitch-bend behavior is not certified.
- State Load/Rewind discards continuing SE; recovery rebuilds BGM and later events. FFTA reconstruction can take longer. Existing ownership guards may retain native fallback.
- Some detected titles still use native fallback. B3DJ, BFTJ, BIXJ, AB2J, A5BJ and BKRJ remain unconfirmed in Phase9.
- Unverified titles may produce incorrect audio, freeze, crash or affect saves. Back up saves/states and use Conservative or Disabled if problems occur. Pointer/memory validation, queue/buffer limits, timeouts and recovery guards remain enabled.
- GB/GBC Fixed Audio remains research. Other titles/scenes and unreviewed audio remain HUMAN_REVIEW_REQUIRED.

## Package, source and licenses

Release assets: `mgba-fixed-audio-tempo-v0.4-preview-win64.zip` and `SHA256SUMS.txt`. No ROM, BIOS, save, state or private audio is included. Public v0.3 tags, Release and all existing assets are preserved.

The ZIP includes core/MP2K bridge/dependency DLLs, Compatibility DB, complete corresponding source, editable bridge/agbplay sources, deterministic patch recipes, original licenses and LGPL build/relink support. mGBA retains MPL-2.0; bridge/agbplay retain LGPLv3; compatibility metadata retains CC BY-SA 4.0 attribution. A modified-library rebuild/relink was validated using supplied materials. See [relink instructions](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/docs/RELINKING.md) and [license audit](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/docs/LICENSE_AUDIT.md).

ZIP SHA256: `025346D077C7E2DD201231D3A3E79DB459FC32D6F2ED986507E5584222EB7FAC`

Core SHA256: `92BAE49862CC8598B05C2E83F973FA969374B12260A0D40441D2793134F93D7C`

Bridge SHA256: `D750BD152B51F632C37F627FC22C6A33E8CDA9B43B16294334D86E90DD511502`
