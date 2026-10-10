# mGBA Fixed Audio Tempo v0.4-preview

Windows x64 Prerelease。BGM優先・既知制約付きの公開です。

## ゲームは2x / 3x、BGMは通常テンポ・通常音程

Independent Audio Clockとイベント駆動合成により、対応GBAのBGMを倍速ゲーム進行から独立して再生します。音声タイムストレッチ・音高補正は使用しません。MP2K ROM_PLAYER/EWRAM_PLAYERは実験的経路、B6JJは専用backendです。

**3x Fixed AudioにはExperimentalを使用してください。** Conservativeは既存の限定試験済み1x/2x経路を維持し、3xではnative音声になります。Disabledは通常mGBA音声です。モード変更後はコンテンツを再読み込みしてください。未知速度・Unlimited・不正なruntime構造ではnative fallbackを使用し、fallback中は倍速時のテンポ維持を行いません。

## 対応DB・検証範囲

[Compatibility DB](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/COMPATIBILITY.md)は公開DATの正確な**3,075 identity**、Experimental試行対象**601 identity**、限定場面のBGM一致**20 identity**（ROM_PLAYER10／EWRAM_PLAYER9／UNKNOWN候補1）です。BGM限定結果は一般的なFixed Audio状態・SE試験とは別管理です。全編・全場面保証ではなく、地域・revision違いに結果を転用しません。

最終準備で**48/48 PCM/event、16/16比較、12/12既存suite、代表3x 36/36**を確認しました。48条件は共通修正版bridge基準、16条件は元v0.3配布package基準8件と共通修正版bridge基準8件を区別しています。実RetroArchのGBA内部frameCounterで、実2x **1.9916～2.0010x**、実3x **2.9683～3.0013x**を測定しました。試験場面に限定した結果で、常に3.000xに達する保証ではありません。[最終監査](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/docs/V04_FINAL_RELEASE_AUDIT.md)。

## デモ動画（公開後に更新したリンク）

公開デモを**RTSSなしの新規録画（1080p・60fps）**に更新しました。大型のGAME SPEED／BGM TEMPO表示と、速度切替時0.3秒の強調演出を追加しています。

- [Oriental Blue：連続歩行（24.87秒）](https://youtu.be/kqMENTHstSE)
- [FFTA：実プレイ（34.32秒）](https://youtu.be/8lcoH3Lm_bE)

両動画とも実RetroArch画面とWindows音声で**1x→2x→3xを2周**します。映像の擬似倍速・音声タイムストレッチ・音高補正はありません。フレーム、オーバーレイ、音声比較の機械的検証はPASSしましたが、**新録画のBGM・SE・AV同期の人間による聴取確認は未完了**です。旧動画のユーザー視聴PASSとハッシュは[公開時の動画manifest](V04_DEMO_MANIFEST.json)に履歴として残します。この公開後のリンク更新はタグ・ZIP・DLL・互換性DBを変更しません。動画ファイルはGit・ZIPに含みません。
## 既知制約・安全上の注意

- 一部の有限SEは2x/3xで途中終了します。BGMを優先し、全SEの寿命・ループ・キャンセル・pitch bendは完全認証していません。
- Load/Rewind時は継続SEを破棄し、BGMと後続イベントを再構築します。FFTA復元に時間がかかる場合や、ownership guardによりnative fallbackが続く場合があります。
- 一部タイトルはnative fallbackです。Phase9のB3DJ／BFTJ／BIXJ／AB2J／A5BJ／BKRJは未確認です。
- 未検証タイトルでは音声異常・フリーズ・クラッシュ・saveへの影響があり得ます。save/stateをバックアップし、問題があればConservative／Disabledを使用してください。メモリ・ポインタ検証、queue/buffer制限、timeout、復元保護は維持しています。
- GB/GBCは研究段階です。他タイトル・他場面・未聴取音声はHUMAN_REVIEW_REQUIREDです。

## 配布・ソース・ライセンス

Assetsは`mgba-fixed-audio-tempo-v0.4-preview-win64.zip`と`SHA256SUMS.txt`です。ROM・BIOS・save・state・非公開音声を含めません。公開v0.3のtag・Release・既存Assetsは保全します。

ZIPにはcore・MP2K bridge・依存DLL・DB、完全な対応ソース、編集可能なbridge/agbplayソース、決定的なpatch recipe、元ライセンス、LGPLビルド／再リンク素材を同梱します。mGBAはMPL-2.0、bridge/agbplayはLGPLv3、互換性メタデータは出典付きCC BY-SA 4.0です。同梱素材だけでの改変library再ビルド・再リンク試験を確認済みです。[再リンク手順](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/docs/RELINKING.md)・[ライセンス監査](https://github.com/granbone/mgba-fixed-audio-tempo/blob/main/docs/LICENSE_AUDIT.md)。
