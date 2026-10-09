# mGBA Fixed Audio Tempo v0.3-preview

Windows x64向けの実験的RetroArchコアです。対応するGBA音源について、実際の2xプレイ中もBGM・SEのテンポと音程を通常速度に保つことを目的とします。

既存v0.2-previewのtag・Release・DLLは**Private保管庫**に維持しています。現行の公開配布リンクとしては案内しません。このrepoは新しいクリーンなrootから始まり、旧Git履歴・tagを取り込みません。

## ダウンロードと導入

v0.3-previewはPrereleaseです。core・bridge・依存DLL、完全な対応ソース、ライセンス・LGPL再リンク素材を同梱します。配布リンクはAssets検証後に掲載します。

[INSTALL](INSTALL.md)・[BUILDING](BUILDING.md)・[ARCHITECTURE](ARCHITECTURE.md)・[再リンク手順](docs/RELINKING.md)を参照してください。bridgeはcore DLLの隣に配置します。

## Fixed Audio Tempo Mode

| モード | 動作 |
| --- | --- |
| **Experimental — All Detected Drivers** | 新規標準設定。実装済みbackendが静的に適用可能と判断したドライバは、未検証タイトルでも試行します。runtime検証により通常音声へ切り替わることがあります。 |
| **Conservative — Tested Drivers Only** | 既存の限定試験済み経路・許可リストに絞ります。 |
| **Disabled** | 通常のmGBA音声。明示的に保存されたDisabled設定を勝手に有効化しません。 |

変更後は**コンテンツの再読み込みが必要**です。対応する2x経路でBGM・SEのテンポと音程を通常速度に維持します。静的試行可能・runtime ACTIVE・音声試験PASSは別の状態です。適用不能・危険な構造ではnative fallbackを使用し、倍速中の通常音声ではテンポ維持を保証しません。

**実験機能：** 未検証ゲームでは、クラッシュ、フリーズ、音声異常、セーブデータやステートセーブの破損等が発生する可能性があります。試用前にバックアップしてください。問題があればConservativeまたはDisabledへ切り替えてください。境界・ポインタ・構造検証、queue/buffer容量制限、timeout、State Load/Rewind保護、安全なfallbackは維持しています。

**3xは未検証・対応保証なし。GB/GBC Fixed Audioは研究段階です。** long-rate復帰ではownership保護によりnative状態が継続する既存制限があります。[試験範囲と制限](docs/V03_RC_VALIDATION.md)

## Compatibility Database v2

[Markdown](COMPATIBILITY.md)・[JSON](compatibility/gba-compatibility.json)・[CSV](compatibility/gba-compatibility.csv)・[Excel](compatibility/GBA_Compatibility.xlsx)。JSONを正本とし、公開DATの正確なidentityからタイトル・地域・revisionを維持します。[出典](compatibility/METADATA_SOURCES.md)

「動作確認済み（限定試験）」は、特定の場面においてFixed Audioの動作を確認したことを示します。ゲームを最後までプレイしたことや、すべてのBGM・SE・機能が正常に動作することを保証するものではありません。

「実験的対応（動作未確認）」にはFixed Audioを試行する機能がありますが、正常動作は未確認です。3,075件すべて、または試行対象601件すべての動作保証ではありません。海外版・別revisionへ試験結果を自動転用しません。

## Technical Demonstrations

<!-- verified-release-links:start -->
ユーザー採用済みのv0.3 RC動画2本をPrivateのPrereleaseへ添付・検証しています。未確認URLは掲載しません。
<!-- verified-release-links:end -->

実際のRetroArchを**1x → 2x → 1x**へ切り替え、OBSとWindows出力音声1系統で収録しています。映像速度変更、音声のタイムストレッチ・音高補正・差し替えはありません。録画core SHA256は `A4BD66BA6A50CFC59109971DC1937BD3675991EE5E302FD3C1554F93C114BBF1`。配布にも同じRC DLLの実バイトを使用します。クリーンな公開ソースの別再ビルドも検証し、Git由来のversion文字列等は異なる場合があります。

完成動画2本はユーザー採用済みで、ゲーム映像・音声の権利関係もユーザー確認済みとして記録します。独自に第三者許諾を取得・認証したという意味ではありません。限定的な戦闘・歩行場面であり、BGM/SE全曲・全場面の動作保証ではありません。[収録証拠・測定限界](docs/V03_DEMO_REVIEW.md)。旧v0.2の撤回動画は使用しません。

ゲーム権利表記：FFTA — © 2003 SQUARE / SQUARE ENIX, All rights reserved. Oriental Blue — © 2003 HUDSON SOFT / © 2003 RED, Licensed to Nintendo。プロジェクトのゲーム所有権や公式な提携を示すものではありません。

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
