# mGBA Fixed Audio Tempo v0.4-preview

Prerelease候補です。ローカル準備後、最終公開指示を待ちます。
tag候補：`v0.4-preview`。v0.4のReleaseリンクはまだありません。

- 独立Audio Clockとイベント駆動合成により、実際の2x/3xゲーム進行中も
  対応GBAのBGMを通常テンポ・通常音程に維持します。
- Experimental／Conservative／Disabled、明示Disabled設定、ownership検証、
  安全なnative fallbackを維持します。
- MP2K player優先度とPSG同順位選択を改善し、既存の限定SE寿命修正と
  B6JJ専用backendを維持します。
- 公開DATの20 identityに限定場面BGMの試験根拠を追加します。
  DBは3,075 identity、Experimental試行対象601件。全編・全場面・SEの保証ではありません。
- 視聴済みのFFTA戦闘・Oriental Blue連続歩行を正式デモ候補へ採用します。
  両方とも1x→2x→3xを2周。FFTA約19.15秒のstate再読み込み・音声カットは明示済みです。
  ユーザーは視聴範囲の映像・音声に重大な違和感なしと確認しました。

既知制約：一部の有限SEは2x/3xで途中終了します。Load/Rewindでは継続SEを破棄します。
FFTAの復元に時間がかかる場合、runtime検証でnative音声が継続する場合があります。
未検証ゲームでは音声異常・フリーズ・クラッシュがあり得ます。未知速度・Unlimitedは
通常音声へ戻し、fallback中は倍速時のテンポ維持を行いません。GB/GBCは研究段階です。
Phase9未確認はB3DJ／BFTJ／BIXJ／AB2J／A5BJ／BKRJです。

Windows x64 ZIPにcore・MP2K bridge・依存DLL・DB・完全な対応ソース・ビルド／再リンク素材・
元ライセンス・全ファイルSHA256を同梱します。変更していない2本のMP4とasset SHA256SUMSは
別Assetsです。ROM・BIOS・save・state・非公開音声は含めません。公開v0.3は保全します。
正確な試験範囲・未聴取項目は[最終監査](V04_FINAL_RELEASE_AUDIT.md)を参照してください。
