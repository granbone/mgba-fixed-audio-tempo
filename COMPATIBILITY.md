# GBA Fixed Audio Compatibility — v0.4-preview RC (local only)

[All games: Excel](compatibility/GBA_Compatibility.xlsx) · [JSON](compatibility/gba-compatibility.json) · [CSV](compatibility/gba-compatibility.csv)

Search the DAT title, region and revision. Driver detection, Experimental trial eligibility and actual limited audio verification are separate fields. A matching title or game code never transfers results across ROM hashes.

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

Limited test pass means Fixed Audio was observed to work in specific test scenarios. It does not indicate a complete playthrough or guarantee correct behavior for every scene, BGM, sound effect, or feature.

「動作確認済み（限定試験）」は、特定の場面においてFixed Audioの動作を確認したことを示します。ゲームを最後までプレイしたことや、すべてのBGM・SE・機能が正常に動作することを保証するものではありません。

Experimental Unverified: Fixed Audioを試行する機能はありますが、正常動作は未確認です。 Fixed Audio can be tried; correct behavior is unverified. Live validation may select native audio.

Experimental Feature. Unverified games may crash, freeze, produce incorrect audio, or experience save-data loss. Back up saves and save states before testing. Use the Conservative or Disabled mode if problems occur.

未検証のゲームでは、クラッシュ、フリーズ、音声異常、セーブデータやステートセーブの破損等が発生する可能性があります。試用前にバックアップを作成してください。問題がある場合はConservativeまたはDisabledへ切り替えてください。動作保証のない実験版です。

## Mode behavior

The new default is Experimental — All Detected Drivers. It tries implemented MP2K ROM_PLAYER, validated EWRAM structures and the exact B6JJ backend. Conservative — Tested Drivers Only uses exact limited-test identities. Disabled uses native audio. Reload content after changing the mode.

Static profile rejection and observed native fallback are different results. Unknown drivers are not assumed unsupported. Unanalyzed DAT identities are not assumed eligible.

3x is supported experimentally for supported backends. Scoped BGM evidence is separate from general/SE validation; unknown or Unlimited rates safely use native audio. GB/GBC Fixed Audio remains research only. FFTA/AFXJ late-state reconstruction can retain native audio for about 18 seconds. Failed reconstruction safely stops the candidate route.

## Status definitions

**LIMITED_TEST_PASS** — 動作確認済み（限定試験） / Limited test pass. Fixed Audio worked in specific test scenarios; no complete playthrough or guarantee for every scene, BGM, sound effect, or feature.

**EXPERIMENTAL_UNVERIFIED** — 実験的対応（動作未確認） / Experimental unverified. Fixed Audioを試行する機能はありますが、正常動作は未確認です。 Static backend checks passed; live checks can still select native audio.

**NATIVE_FALLBACK** — 通常音声へ切替 / Native fallback observed in a limited runtime test. A scenario result, not a permanent driver verdict.

**UNSUPPORTED** — 現行実装では未対応 / Current backend rejected the analyzed identity. Native emulation remains available.

**UNKNOWN_DRIVER** — 音源方式未特定 / Analysis exists but driver family is unidentified. This does not establish unsupported behavior.

**NOT_ANALYZED** — 未解析 / Public DAT identity without reliable exact-identity driver analysis.

## Metadata and reports

Titles, regions and named revisions come exclusively from the pinned public No-Intro DAT. Header revisions are separate. Unmatched analysis stays in private ignored reports. [Sources and licenses](compatibility/METADATA_SOURCES.md).

Use the [compatibility report form](.github/ISSUE_TEMPLATE/compatibility-report.yml). Include exact hashes, mode, speed, core version and scenario. Do not attach ROMs, BIOS files, saves or savestates.


## BGM-only verification

20 Phase9 exact identities have scoped Fixed1x/2x/3x BGM comparisons. All26 tested identities were already catalogued; zero overlap with the prior32 Limited Test Pass identities. General status, Conservative eligibility and SE acceptance are unchanged. These are not complete-playthrough claims.

| Code | Exact release | BGM result | Speeds |
|---|---|---|---|
| BTAJ | Astro Boy - Tetsuwan Atom - Atom Heart no Himitsu (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| AE2J | Battle Network - Rockman EXE 2 (Japan) (Rev 1) | BGM_CONFIRMED | [1, 2, 3] |
| A6BJ | Battle Network - Rockman EXE 3 (Japan) (Rev 1) | BGM_CONFIRMED | [1, 2, 3] |
| AXBJ | Black Matrix Zero (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| BFGJ | Bokujou Monogatari - Mineral Town no Nakama-tachi for Girl (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| AJZJ | Bomberman Jetters - Densetsu no Bomberman (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| ABFJ | Breath of Fire - Ryuu no Senshi (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| AB2J | Breath of Fire II - Shimei no Ko (Japan) | BGM_INCONCLUSIVE | [1, 2, 3] |
| A2BJ | Bubble Bobble - Old & New (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| BIXJ | Calciobit (Japan) | BGM_NOT_ACTIVE | [1, 2, 3] |
| A2CJ | Castlevania - Akatsuki no Minuet (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| ACHJ | Castlevania - Byakuya no Concerto (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| AGDJ | Chinmoku no Iseki - Estpolis Gaiden (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| A5BJ | Chocobo Land - A Game of Dice (Japan) | BGM_INCONCLUSIVE | [1, 2, 3] |
| ACGJ | Columns Crown (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| ARJJ | Custom Robo GX (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| ASTJ | Densetsu no Stafy (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| B3DJ | Densetsu no Stafy 3 (Japan) (Rev 1) | BGM_NOT_ACTIVE | [1, 2, 3] |
| BDDJ | Double Dragon Advance (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| A9HJ | Dragon Quest Monsters - Caravan Heart (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| BFTJ | F-Zero - Climax (Japan) | BGM_NOT_ACTIVE | [1, 2, 3] |
| BFZJ | F-Zero - Falcon Densetsu (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| AGAJ | Gradius Generation (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| BGXJ | Gunstar Super Heroes (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| BGYJ | Konjiki no Gashbell!! - Unare! Yuujou no Zakeru 2 (Japan) | BGM_CONFIRMED | [1, 2, 3] |
| BKRJ | No No No Puzzle Chailien (Japan) | BGM_INCONCLUSIVE | [1, 2, 3] |

See bgm_test_scope, bgm_tested_commit and bgm_evidence in JSON/CSV/XLSX. HUMAN_REVIEW_REQUIRED applies to unlistened audio. Legacy fixed_audio_3x records general validation only; BGM fields contain the scoped3x evidence. Finite SE can end early at2x/3x. Load/Rewind discard continuing SE; AAMJ can use ownership fallback, and late FFTA recovery can take about18 seconds.
