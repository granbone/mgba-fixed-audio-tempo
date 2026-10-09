# mGBA Fixed Audio Tempo v0.3-preview

An experimental Windows x64 RetroArch core that aims to keep supported GBA BGM and sound effects at normal tempo and pitch during actual 2x gameplay.

The existing v0.2-preview tag, Release and binaries are preserved in the **Private archive**. They are not current public downloads. This repository begins with a new clean root; no former Git history or tags are imported.

## Download and installation

The v0.3-preview prerelease contains the core, bridge, dependency DLL, complete corresponding source, license notices and LGPL rebuild/relink materials. Download links are below; per-file SHA256 is supplied with the release.

See [INSTALL.md](INSTALL.md), [BUILDING.md](BUILDING.md), [architecture](ARCHITECTURE.md), [LGPL relinking](docs/RELINKING.md) and [third-party notices](THIRD_PARTY_NOTICES.md). The bridge is installed beside the core DLL.

## Fixed Audio Tempo Mode

| Mode | Behavior |
| --- | --- |
| **Experimental — All Detected Drivers** | New default. Tries implemented backends for statically eligible detected drivers, including unverified titles. Runtime validation can still select native audio. |
| **Conservative — Tested Drivers Only** | Restricts the attempt to the existing tested paths/allowlists. |
| **Disabled** | Uses normal mGBA audio. Explicitly saved Disabled settings remain disabled. |

Reload content after changing modes. In supported 2x paths, BGM/SE are rendered at normal tempo and pitch. Static eligibility, runtime ACTIVE and an actual audio test pass are different observations. Unsupported or unsafe runtime structures use native fallback; fallback does not preserve fixed tempo during fast-forward.

**Experimental Feature:** Unverified games may crash, freeze, produce incorrect audio, or experience save-data loss. Back up saves and save states before testing. Use Conservative or Disabled if problems occur. Memory checks, structure/pointer validation, buffer/queue limits, timeouts, State Load/Rewind protection and safe fallback remain enabled. Backup advice does not replace these safeguards.

**3x is unvalidated and not guaranteed. GB/GBC Fixed Audio remains research.** A long-rate recovery/ownership limitation can retain native fallback; see [validation and limits](docs/V03_RC_VALIDATION.md).

## Compatibility Database v2

Browse [COMPATIBILITY.md](COMPATIBILITY.md), [JSON](compatibility/gba-compatibility.json), [CSV](compatibility/gba-compatibility.csv), or [Excel](compatibility/GBA_Compatibility.xlsx). JSON is the source of truth. Titles, regions and revisions come from exact public DAT identities; [metadata sources](compatibility/METADATA_SOURCES.md) retain attribution and license terms.

Limited test pass means Fixed Audio was observed to work in specific test scenarios. It does not indicate a complete playthrough or guarantee correct behavior for every scene, BGM, sound effect, or feature.

Experimental Unverified means a Fixed Audio attempt exists, but correct operation is unverified. Neither all 3,075 releases nor all 601 eligible identities are guaranteed to work. Regional variants do not inherit another release's results.

## Technical Demonstrations

<!-- verified-release-links:start -->
| Game | Scene / 場面 | Video |
| --- | --- | --- |
| Final Fantasy Tactics Advance (AFXJ) | Battle Scene / 戦闘・55s | [MP4](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.3-preview/FFTA_Fixed_Audio_v03_Demo.mp4) |
| Oriental Blue: Ao no Tengai (AORJ) | Walking / Gameplay・55s | [MP4](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.3-preview/OrientalBlue_Fixed_Audio_v03_Demo.mp4) |

[Windows x64 ZIP](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.3-preview/mgba-fixed-audio-tempo-v0.3-preview-win64.zip) · [SHA256SUMS](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.3-preview/SHA256SUMS.txt) · [v0.3-preview Release](https://github.com/granbone/mgba-fixed-audio-tempo/releases/tag/v0.3-preview)
<!-- verified-release-links:end -->

Both recordings show real RetroArch gameplay switching **1x → 2x → 1x**, recorded through OBS and one actual Windows audio-output source. No video speed change, audio time-stretch, pitch correction or audio replacement was applied. 

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
