# mGBA Fixed Audio Tempo — v0.4-preview

## 2x / 3x gameplay with normal BGM tempo and pitch

This experimental Windows x64 RetroArch core uses an independent audio clock and event-driven synthesis to keep supported GBA BGM at its normal tempo and pitch while the game runs at **2x or 3x**. It does not time-stretch or pitch-correct a fast recording. Native fallback uses normal mGBA audio and does not provide fixed tempo during fast-forward.

Get the **v0.4-preview Windows x64 prerelease** from [GitHub Releases](https://github.com/granbone/mgba-fixed-audio-tempo/releases). Public v0.3 downloads below remain available and unchanged. See [final release audit](docs/V04_FINAL_RELEASE_AUDIT.md) and [release notes](docs/V04_RELEASE_NOTES_EN.md).

Phase9 verified BGM in limited scenes for **20 exact DAT identities** (ROM_PLAYER 10, EWRAM_PLAYER 9, UNKNOWN candidate 1). These are additional BGM-only observations, not whole-game or SE certification. The catalog has 3,075 identities; 601 are eligible for an Experimental attempt. See [compatibility](COMPATIBILITY.md).

Known limits: some finite SE can end early at 2x/3x; ongoing SE are discarded on State Load/Rewind; FFTA restoration can take longer; ownership or runtime checks may keep native audio active. Unknown/Unlimited frontend speeds use native audio. Unverified games can produce incorrect audio, freeze or crash. GB/GBC Fixed Audio remains research. Major BGM failures, runaway audio and new compatibility regressions remain release blockers.

## v0.4 demonstrations — updated videos

The current public videos are **new RTSS-free recordings** captured at **1920×1080 / 60 fps**, with prominent GAME SPEED 1x / 2x / 3x and BGM TEMPO 1x overlays and 0.3-second speed-switch highlighting. They show real RetroArch gameplay and Windows audio, with **1x→2x→3x twice**. No artificial video speed-up or audio time-stretch/pitch correction was applied.

- [Oriental Blue: Ao no Tengai — continuous walking (24.87 s)](https://youtu.be/kqMENTHstSE)
- [Final Fantasy Tactics Advance — active gameplay (34.32 s)](https://youtu.be/8lcoH3Lm_bE)

Automated video-frame, overlay-transition and audio-comparison checks passed. **Human listening review of these new recordings (BGM, SE, A/V sync) is still pending.** The earlier user-approved demo footage and its original hashes are documented in the [historical launch manifest](docs/V04_DEMO_MANIFEST.json); that approval does not certify the replacement recordings. The updated videos are not stored in Git or the release ZIP.
## Download and installation

The v0.4-preview prerelease contains the core, bridge, dependency DLL, complete corresponding source, license notices and LGPL rebuild/relink materials. [Windows x64 ZIP](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.4-preview/mgba-fixed-audio-tempo-v0.4-preview-win64.zip) · [SHA256SUMS](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.4-preview/SHA256SUMS.txt) · [Release notes](https://github.com/granbone/mgba-fixed-audio-tempo/releases/tag/v0.4-preview). Verify the ZIP checksum before installation. Per-file SHA256 is also included inside the ZIP.

See [INSTALL.md](INSTALL.md), [BUILDING.md](BUILDING.md), [architecture](ARCHITECTURE.md), [LGPL relinking](docs/RELINKING.md) and [third-party notices](THIRD_PARTY_NOTICES.md). The bridge is installed beside the core DLL.

## Fixed Audio Tempo Mode

| Mode | Behavior |
| --- | --- |
| **Experimental — All Detected Drivers** | New default. Tries implemented backends for statically eligible detected drivers, including unverified titles. Runtime validation can still select native audio. |
| **Conservative — Tested Drivers Only** | Restricts the attempt to existing tested paths/allowlists at1x/2x. At3x it uses native audio. |
| **Disabled** | Uses normal mGBA audio. Explicitly saved Disabled settings remain disabled. |

Reload content after changing modes. Supported2x/3x BGM paths use normal tempo and pitch; finite SE lifetime has the limitations listed above. Static eligibility, runtime ACTIVE and an actual audio test pass are different observations. Unsupported or unsafe runtime structures use native fallback; fallback does not preserve fixed tempo during fast-forward.

**Experimental Feature:** Unverified games may crash, freeze, produce incorrect audio, or experience save-data loss. Back up saves and save states before testing. Use Conservative or Disabled if problems occur. Memory checks, structure/pointer validation, buffer/queue limits, timeouts, State Load/Rewind protection and safe fallback remain enabled. Backup advice does not replace these safeguards.

Published v0.3 scope: **3x is unvalidated and not guaranteed. GB/GBC Fixed Audio remains research.** A long-rate recovery/ownership limitation can retain native fallback; see [validation and limits](docs/V03_RC_VALIDATION.md).

## Compatibility Database v3

Browse [COMPATIBILITY.md](COMPATIBILITY.md), [JSON](compatibility/gba-compatibility.json), [CSV](compatibility/gba-compatibility.csv), or [Excel](compatibility/GBA_Compatibility.xlsx). JSON is the source of truth. Titles, regions and revisions come from exact public DAT identities; [metadata sources](compatibility/METADATA_SOURCES.md) retain attribution and license terms.

Limited test pass means Fixed Audio was observed to work in specific test scenarios. It does not indicate a complete playthrough or guarantee correct behavior for every scene, BGM, sound effect, or feature.

Experimental Unverified means a Fixed Audio attempt exists, but correct operation is unverified. Neither all 3,075 releases nor all 601 eligible identities are guaranteed to work. Regional variants do not inherit another release's results.

## Community reports and licenses

Use the [compatibility Issue Form](https://github.com/granbone/mgba-fixed-audio-tempo/issues/new?template=compatibility-report.yml). Include exact identity, mode, speed, scene and version. Do not upload ROMs, BIOS, saves or savestates.

mGBA source retains MPL-2.0; separate bridge/agbplay retain LGPLv3 and their original notices; compatibility metadata retains CC BY-SA 4.0 with No-Intro/libretro attribution. Source/relink archives are in `source/` and in the distribution. [License audit](docs/LICENSE_AUDIT.md). No game binaries, firmware, saves, states or video files are committed.

[日本語](README_JP.md)

<!-- compatibility-summary:start -->
| Driver Family | Limited Test Pass | Experimental Unverified | Native Fallback | Unsupported | Unknown Driver | Not Analyzed | Trial eligible | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| MP2K_ROM_PLAYER | 2 | 355 | 2 | 4 | 0 | 0 | 359 | 363 |
| MP2K_EWRAM_PLAYER | 29 | 212 | 0 | 0 | 0 | 0 | 241 | 241 |
| MP2K_MIXED / OTHER_MP2K | 0 | 0 | 0 | 4 | 0 | 0 | 0 | 4 |
| CUSTOM_DRIVER | 1 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| OTHER_DRIVER | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 |
| UNKNOWN_DRIVER | 0 | 0 | 4 | 0 | 183 | 0 | 0 | 187 |
| NOT_ANALYZED | 0 | 0 | 0 | 0 | 0 | 2278 | 0 | 2278 |
| Total | 32 | 567 | 6 | 9 | 183 | 2278 | 601 | 3075 |

Counts refer to unique public DAT release identities. Regional releases and revisions do not inherit test results. Trial eligible includes limited-pass identities; it is not an additional status. NOT_ANALYZED is shown separately.

Trial eligible = 32 limited passes + 567 unverified trials + 2 observed fallback (AG7J, U3IJ). Static trial eligibility can coexist with native fallback in a tested scene; neither detection nor ACTIVE is an audio test pass. 静的に試行可能でも、試験した場面では通常音声になる場合があります。ACTIVEだけで音声試験PASSとは認定しません。

<!-- compatibility-counts {"driver_families":{"CUSTOM_DRIVER":{"EXPERIMENTAL_UNVERIFIED":0,"LIMITED_TEST_PASS":1,"NATIVE_FALLBACK":0,"NOT_ANALYZED":0,"UNKNOWN_DRIVER":0,"UNSUPPORTED":0,"experimental_trial_eligible":1,"total":1},"MP2K_EWRAM_PLAYER":{"EXPERIMENTAL_UNVERIFIED":212,"LIMITED_TEST_PASS":29,"NATIVE_FALLBACK":0,"NOT_ANALYZED":0,"UNKNOWN_DRIVER":0,"UNSUPPORTED":0,"experimental_trial_eligible":241,"total":241},"MP2K_MIXED / OTHER_MP2K":{"EXPERIMENTAL_UNVERIFIED":0,"LIMITED_TEST_PASS":0,"NATIVE_FALLBACK":0,"NOT_ANALYZED":0,"UNKNOWN_DRIVER":0,"UNSUPPORTED":4,"experimental_trial_eligible":0,"total":4},"MP2K_ROM_PLAYER":{"EXPERIMENTAL_UNVERIFIED":355,"LIMITED_TEST_PASS":2,"NATIVE_FALLBACK":2,"NOT_ANALYZED":0,"UNKNOWN_DRIVER":0,"UNSUPPORTED":4,"experimental_trial_eligible":359,"total":363},"NOT_ANALYZED":{"EXPERIMENTAL_UNVERIFIED":0,"LIMITED_TEST_PASS":0,"NATIVE_FALLBACK":0,"NOT_ANALYZED":2278,"UNKNOWN_DRIVER":0,"UNSUPPORTED":0,"experimental_trial_eligible":0,"total":2278},"OTHER_DRIVER":{"EXPERIMENTAL_UNVERIFIED":0,"LIMITED_TEST_PASS":0,"NATIVE_FALLBACK":0,"NOT_ANALYZED":0,"UNKNOWN_DRIVER":0,"UNSUPPORTED":1,"experimental_trial_eligible":0,"total":1},"UNKNOWN_DRIVER":{"EXPERIMENTAL_UNVERIFIED":0,"LIMITED_TEST_PASS":0,"NATIVE_FALLBACK":4,"NOT_ANALYZED":0,"UNKNOWN_DRIVER":183,"UNSUPPORTED":0,"experimental_trial_eligible":0,"total":187}},"experimental_trial_eligible":601,"public_status":{"EXPERIMENTAL_UNVERIFIED":567,"LIMITED_TEST_PASS":32,"NATIVE_FALLBACK":6,"NOT_ANALYZED":2278,"UNKNOWN_DRIVER":183,"UNSUPPORTED":9},"total_releases":3075} -->
<!-- compatibility-summary:end -->
