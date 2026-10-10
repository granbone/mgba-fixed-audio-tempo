# mGBA Fixed Audio Tempo — v0.4-preview

## ゲームは2x / 3x、BGMは通常テンポ・通常音程

Windows x64向けの実験的RetroArchコアです。独立Audio Clockとイベント駆動合成により、対応するGBAのBGMを、**ゲームが2x・3xでも通常テンポ・通常音程**で再生します。録音のタイムストレッチや音高補正は使用しません。native fallbackでは通常のmGBA音声になり、倍速時のテンポ維持は行いません。

**v0.4-preview Windows x64 Prerelease**は[GitHub Releases](https://github.com/granbone/mgba-fixed-audio-tempo/releases)から取得してください。下記の公開v0.3配布物は保全しています。[最終公開監査](docs/V04_FINAL_RELEASE_AUDIT.md)・[Release Notes](docs/V04_RELEASE_NOTES_JP.md)。

Phase9では公開DATの正確な**20 identityで限定場面のBGM一致**を確認しました（ROM_PLAYER10、EWRAM_PLAYER9、UNKNOWN候補1）。全編やSEの認証ではありません。DBは3,075 identity、Experimental試行対象601 identityです。[対応表](COMPATIBILITY.md)。

既知制約：2x/3xで一部の有限SEが途中終了する場合があります。Load/Rewindでは継続中SEを破棄します。FFTAの復元に時間がかかる場合、ownershipやruntime検証でnative音声が続く場合があります。未知速度・Unlimitedは通常音声へ戻します。未検証ゲームには音声異常・フリーズ・クラッシュの可能性があります。GB/GBC Fixed Audioは研究段階です。重大なBGM異常・音声暴走・新規互換性回帰は公開阻害条件です。

## v0.4 デモ動画（更新版）

RTSS表示を除去した1080p・60fpsの新録画に更新しました。GAME SPEED 1x / 2x / 3xとBGM TEMPO 1xを大きく表示し、切替時に0.3秒の強調を加えています。

- **[Oriental Blue — 連続歩行（24.87秒）](https://youtu.be/kqMENTHstSE)**
- **[FFTA — 戦闘実プレイ（34.32秒）](https://youtu.be/8lcoH3Lm_bE)**

両動画とも実際のRetroArchで1x → 2x → 3xを2周します。録画の擬似倍速、音声のタイムストレッチ、音高補正はありません。

## ダウンロードと導入

v0.4-previewはPrereleaseです。core・bridge・依存DLL、完全な対応ソース、ライセンス・LGPL再リンク素材を同梱します。[Windows x64 ZIP](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.4-preview/mgba-fixed-audio-tempo-v0.4-preview-win64.zip)・[SHA256SUMS](https://github.com/granbone/mgba-fixed-audio-tempo/releases/download/v0.4-preview/SHA256SUMS.txt)・[Release Notes](https://github.com/granbone/mgba-fixed-audio-tempo/releases/tag/v0.4-preview)。導入前にZIPのチェックサムを照合してください。ZIP内にも全ファイルのSHA256を含みます。

[INSTALL](INSTALL.md)・[BUILDING](BUILDING.md)・[ARCHITECTURE](ARCHITECTURE.md)・[再リンク手順](docs/RELINKING.md)を参照してください。bridgeはcore DLLの隣に配置します。

## Fixed Audio Tempo Mode

| モード | 動作 |
| --- | --- |
| **Experimental — All Detected Drivers** | 新規標準設定。実装済みbackendが静的に適用可能と判断したドライバは、未検証タイトルでも試行します。runtime検証により通常音声へ切り替わることがあります。 |
| **Conservative — Tested Drivers Only** | 既存の1x/2x限定試験済み経路・許可リストに絞ります。3xではnative音声を使用します。 |
| **Disabled** | 通常のmGBA音声。明示的に保存されたDisabled設定を勝手に有効化しません。 |

変更後は**コンテンツの再読み込みが必要**です。対応する2x/3x経路でBGMのテンポと音程を通常速度に維持します。有限SEの寿命には上記の制約があります。静的試行可能・runtime ACTIVE・音声試験PASSは別の状態です。適用不能・危険な構造ではnative fallbackを使用し、倍速中の通常音声ではテンポ維持を保証しません。

**実験機能：** 未検証ゲームでは、クラッシュ、フリーズ、音声異常、セーブデータやステートセーブの破損等が発生する可能性があります。試用前にバックアップしてください。問題があればConservativeまたはDisabledへ切り替えてください。境界・ポインタ・構造検証、queue/buffer容量制限、timeout、State Load/Rewind保護、安全なfallbackは維持しています。

公開v0.3の試験範囲：**3xは未検証・対応保証なし。GB/GBC Fixed Audioは研究段階です。** long-rate復帰ではownership保護によりnative状態が継続する既存制限があります。[試験範囲と制限](docs/V03_RC_VALIDATION.md)

## Compatibility Database v3

[Markdown](COMPATIBILITY.md)・[JSON](compatibility/gba-compatibility.json)・[CSV](compatibility/gba-compatibility.csv)・[Excel](compatibility/GBA_Compatibility.xlsx)。JSONを正本とし、公開DATの正確なidentityからタイトル・地域・revisionを維持します。[出典](compatibility/METADATA_SOURCES.md)

「動作確認済み（限定試験）」は、特定の場面においてFixed Audioの動作を確認したことを示します。ゲームを最後までプレイしたことや、すべてのBGM・SE・機能が正常に動作することを保証するものではありません。

「実験的対応（動作未確認）」にはFixed Audioを試行する機能がありますが、正常動作は未確認です。3,075件すべて、または試行対象601件すべての動作保証ではありません。海外版・別revisionへ試験結果を自動転用しません。


## 報告・ライセンス

[Compatibility Issue Form](https://github.com/granbone/mgba-fixed-audio-tempo/issues/new?template=compatibility-report.yml)へ、正確なidentity・モード・速度・場面・versionを報告してください。ROM・BIOS・save・savestateは添付禁止です。

mGBAはMPL-2.0、別bridge/agbplayはLGPLv3、互換性メタデータはNo-Intro/libretro出典付きCC BY-SA 4.0を維持します。`source/`と配布ZIPにソース・再リンク素材を含みます。[ライセンス監査](docs/LICENSE_AUDIT.md)。ゲーム実データ・BIOS・save・state・動画ファイルはGitへ入れません。

[English](README.md)

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
