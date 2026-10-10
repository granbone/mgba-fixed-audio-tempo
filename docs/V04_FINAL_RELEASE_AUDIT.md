# v0.4-preview final release audit

判定：**FINAL_PACKAGE_VALIDATION_PENDING**。公開操作は行わず、最終公開指示を待つ。
この文書は配布ソースcommit時点の試験スナップショット。最終ZIP・再リンク・導入の検証は次段階。完成後のGit側監査報告が最終判定を記録し、ZIP自身のhash循環を避ける。

## 新規実行と継続証拠

最終配布DLLそのもの（RC1内core/bridge）で新規実行：48/48 PCM/event、16/16、12/12既存suite、代表3x36/36。
48はPhase6の修正済みbridge基準。元v0.3packageそのものとの普遍的な波形一致を意味しない。
16は元v0.3package8条件と共通修正bridge8条件を分離し、PCM/event/初期RAMが一致。
新しい可搬48 replayツールも同じ不変基準へ48/48一致。golden更新・閾値変更なし。
初回B6JJ frontend takeはWindows制御fileの一時共有違反で中断。診断ツールへ最大1秒の再試行を追加し、
失敗証拠を保護してB6JJ全6区間を再実行。DLL/音声codeには変更なし。
36はPhase9とPCM/event完全一致、通常試験のring under/over・queue dropは0。
28安全probeは未知/ゼロ/別速度・部分callback・MP2K容量fault・明示Disabled優先を確認。
故意に注入したMP2K underrun/overrunは各1として記録し、通常試験の0と混同しない。
B6JJのring注入knobは対象外で、独立したlegacy回復・guard試験を参照。

継続証拠：Phase9の26タイトル結果とBGM限定20 identity、旧Windows音声原本、採用済み2動画。
20はROM10/EWRAM9/UNKNOWN候補1。新規catalog行0、従来の一般LIMITED32との重複0。
既知の優先度/有限SE寿命修正による過去PCM差は保持し、今回新たな音声実装修正はない。

## 実RetroArchとGBA内部速度

各10秒、1→2→3→1→2→3を実RetroArchで連続試験。GBA内部frameCounterの実deltaと単調時刻を使用。
設定値・画面FPS・headless throughputを速度証明にしない。裏環境でmuted、物理出力品質の新規聴取ではない。
callback samplesはcore提出/受付でありWindows機器の出力rateではない。

| Game | 1x（2区間） | 2x（2区間） | 3x（2区間） |
|---|---|---|---|
| AAMJ | 1.00459 / 1.00457 | 2.00053 / 2.00015 | 2.96831 / 2.97905 |
| AFXJ | 1.00456 / 1.00460 | 1.99759 / 1.99159 | 2.97651 / 2.97041 |
| AORJ | 1.00459 / 1.00458 | 2.00038 / 2.00101 | 2.99863 / 3.00093 |
| B6JJ | 1.00461 / 1.00459 | 2.00007 / 1.99992 | 3.00130 / 3.00024 |

通常区間のframe step異常・部分callback・ring異常・予期しないfallbackは0。
実RetroArchの1→3→1→3 Load/Rewind/直後切替も4代表で新規実行。復元中のfallbackとSE破棄を許容し、
継続SEの完全復元とは扱わない。AAMJ ownership hard fallbackとFFTA長い復元を維持。
36条件には1x/2x/3x、切替、Load/Rewind、3modeを含む。Conservativeの3x native動作は意図どおり。

## BGM品質と聴取範囲

新規24秒loaded sceneで、各速度の同一時間尺度2秒window6個を既存解析で比較。
一定の発音開始offsetは既存復元guardによるものとして分離し、time stretch・pitch補正は使用しない。

| Game | Speed | フレーズ時間進行率 | 最小波形相関 | 主要周波数一致 |
|---|---|---:|---:|---|
| AAMJ | 2x | 1.000000 | 1.000000 | True |
| AAMJ | 3x | 1.000000 | 1.000000 | True |
| AFXJ | 2x | 1.000000 | 1.000000 | True |
| AFXJ | 3x | 1.000000 | 1.000000 | True |
| AORJ | 2x | 1.000000 | 1.000000 | True |
| AORJ | 3x | 1.000000 | 1.000000 | True |
| B6JJ | 2x | 1.000000 | 1.000000 | True |
| B6JJ | 3x | 1.000000 | 1.000000 | True |

視聴済みFFTA active gameplay34.25秒とOriental Blue active walking24.17秒を変更せず採用。
ユーザー確認：VIDEO_USER_REVIEW_PASS=YES。視聴範囲のBGMテンポ/音程に違和感なし、映像PASS、
音量/SE/AV同期に重大な違和感なし。FFTA約19.15秒のstate再読み込み・音声カットは明示済み。
他タイトルや全SEの認証には拡張しない。[動画manifest](V04_DEMO_MANIFEST.json)。
AORJ192ms問題はUNKNOWN/INCONCLUSIVE。再現しないことを解決済みとせず製品コード変更なし。

## DB・公開構成・保全

3075 identity、Experimental601、BGM限定20を維持。JSON/CSV/XLSX/Markdownは全フィールド照合PASS。
BGMと一般status/SE欄を分け、地域/revisionの転用・全編への昇格なし。
未確認6件：B3DJ/BFTJ/BIXJ NOT_ACTIVE、AB2J/A5BJ/BKRJ INCONCLUSIVE。
開発branchの個人パス付き旧診断履歴は保持し、公開対象にしない。
最新公開main fd06f479から別ローカル公開候補を作成し、コード/必要な公開文書を転記。
私有開発commit/objectをimportしない。公開candidateは独立cloneで全objects/到達不能blobも監査する。
旧auditorのJSON escaped path見逃しを修正。RC1の過去非build証拠は最終source exportから除外し、
全compiled sourceの一致を確認。最初の予備cloneは私的証拠として保持し公開しない。
公開v0.3のtag/Release/3成果物/既存デモを保全、公開mainの既存README更新も維持。
142保護WAV、旧RC1デモ、新採用デモ、既存golden、失敗試験、元ROM/save/stateを変更・削除しない。
NES/PS等の他開発環境には触れない。

DLLはRC1と同一。元binary source d451f54dと公開配布source commitをmanifestで別記。
完全な対応source、bridge build-time patches、依存source/support、LICENSE/NOTICE、LGPL再リンク資料を同梱。
MP4はGit/ZIPへ含めずRelease Assets用。新ZIPはRC1と別hashを記録する。
日英README/Release NotesとYouTube英語metadataを準備し、未作成Releaseの公開リンクは記載しない。

## 既知制約・残る人による確認

有限SEの2x/3x途中終了、Load/Rewind継続SE破棄、FFTA長い復元、native fallbackを明示。
Conservative3xはnative。未検証ゲームは音声異常/フリーズ/クラッシュの可能性。GB/GBCは研究段階。
HUMAN_REVIEW_REQUIRED：AAMJ/B6JJの可聴音色/SE、追加代表8タイトルの聴取素材、承認動画外の
loop/cancel/pitch bend/voice競合、長時間/後半/未検証identity。未聴取をPASSへ昇格しない。
重大なクラッシュ・BGM破綻・新規1x/2x回帰は今回の限定試験で検出されていない。
全タイトルの完全対応を公開条件にしないBGM重視のpreviewとして、既知制約付きの公開候補を提案する。
push/tag/Release/YouTube uploadは実施せず停止する。
