# v0.4-preview BGM-focused Release Candidate

Historical RC1 snapshot. The adopted active demos and final fresh regressions are
reported in [V04_FINAL_RELEASE_AUDIT](V04_FINAL_RELEASE_AUDIT.md). Earlier pending
review statements and25-second videos below are retained as historical evidence,
not the final adopted video selection.

判定：**RC_READY_WITH_KNOWN_LIMITATIONS**。ローカルRCの確認へ進める。
公開操作は未承認で、push・tag・Release・動画uploadは実施しない。
人による新素材の聴取・動画確認は **HUMAN_REVIEW_REQUIRED**。

## 対応範囲とDB

Phase9の26 identityを保全。限定場面BGM一致20件（ROM_PLAYER10／EWRAM_PLAYER9／UNKNOWN候補1）。
20件とも既存DAT catalogと完全一致し、新規catalog行は0。従来の一般的な限定PASS32件との重複は0。
DBへの新しいBGM限定確認は20 identityで、一般的な確認済みを52件へ昇格したものではない。
3,075 identity、従来の全status／eligibility／SE関連値を保持。Conservative対象は変更しない。
bgm_*5列とse_verification_statusを追加、JSON／CSV／XLSX／Markdownを検証。
legacy fixed_audio_3xは一般的検証欄として保持し、限定BGM3x証拠と区別する。

未確認はB3DJ／BFTJ／BIXJ（NOT_ACTIVE）、AB2J／A5BJ／BKRJ（INCONCLUSIVE）。
B3DJは復元位置、BFTJは起動時の既存hard guard、BIXJは有効BGM場面不足。
AB2Jは場面／曲切替、A5BJ・BKRJはSE混合等で比較不能。全編・地域・revisionへ結果を継承しない。

## BGMと実RetroArch

AAMJ／AFXJ／AORJ／B6JJを優先。追加代表はAXBJ／A2CJ／A9HJ／BFZJ（ROM）、
AE2J／ABFJ／ARJJ／BDDJ（EWRAM）。既存PCMから各速度3秒、12本計108秒の手動聴取素材を用意。
原本に変更なし。切出し境界3秒／6秒は実速度切替ではない。1xを基準に楽曲進行、音高、途切れ、
欠落楽器を比較する。通常sample scaleでの自動一致と、人によるnative音色・単独楽器評価を分ける。

代表4タイトルで実RetroArchを連続1→2→3→1→2→3、各8秒で実行。
core->frameCounter()直前／直後の差を単調時刻と対応させて測定し、画面FPSや設定値を使わない。
非公開試験adapterは正式frontend速度APIを要求し、実throttle報告を改変しない。
core／bridge音声コードは変更しておらず、adapterはRC runtimeに含めない。

| Game | 1x（2区間） | 2x（2区間） | 3x（2区間） | ring under/over |
|---|---|---|---|---|
| AAMJ | 1.00456/1.00457 | 1.99946/1.99768 | 3.00734/2.96049 | 0/0 |
| AFXJ | 1.00459/1.00458 | 2.00025/1.99877 | 3.01243/2.99846 | 0/0 |
| AORJ | 1.00455/1.00457 | 1.99975/2.00029 | 2.99626/2.99872 | 0/0 |
| B6JJ | 1.00456/1.00458 | 1.99982/1.99929 | 2.99765/2.99958 | 0/0 |

実3xは2.96049〜3.01243x。端数未達を記録し、音声を犠牲にした調整はしない。
全測定区間Fixed、frame step異常0、callback partial0、予期しないfallback0。
CPUは1core相当の区間全体平均（JSON参照）；標準的なプロセスCPU％との混同を避ける。
callback sample rateはcore提出／受付でありWindows device出力rateではない。

## 回帰・復元・安全

既存48/48、16/16（元v0.3配布package／共通修正bridge比較を分離）、12/12 suite、代表3x36/36を保全。
48・16・36のfull PCM/event証拠を再hash照合。12 suiteは既存成功metadataとログを保全。
**Phase10で全suiteを新規再実行したとは扱わない。** 約5GBのPCMを再生成しない。
追加で最終RC DLLの4秒prefix12本を新規実行し、PCM／event／初期RAM一致、under/over0。
12短期試験はfull suiteの代用やgolden更新ではない。DLLはPhase9 finalと完全に同じbytes。

Experimental／Conservative／Disabled、未知速度・Unlimited fallback、pointer／ownership／queue／ring／timeoutを維持。
復元時は継続SEを破棄する。AAMJ ownership fallback、FFTA後半stateで約18秒の復元制約も維持。
新規の汎用STOP解析・寿命処理の変更はない。有限SEの軽微な2x/3x途中終了は既知制約。
BGM停止・暴走・停止不能loop・クラッシュ・メモリ破損・重大新規回帰は検出されていない（限定試験範囲）。

## 非公開25秒デモ

FFTA25.004秒、OrientalBlue25.000秒のMP4を作成。実RetroArchゲーム映像とWindows WASAPI音声。
連続1→2→3→1→2→3。編集は切出しと内部GBA frame実測字幕だけ、音声AACは原trackをcopy。
映像擬似倍速・音声置換・time stretch・pitch補正なし。RTSS画面FPSはrender表示でゲームframeと異なる。
OBS原本、独立Windows loopback参照、時刻・DLL hash・失敗試験はprivateに保全。
初回genericウィンドウ指定が別RetroArchを捕捉した録画は不採用。正しいGBA画面を確認した2本だけを採用。
以後は隔離portable OBSと正確なmGBAウィンドウタイトルを使用。他エミュレーターへの入力・開発変更なし。
今回作成したOBS stale-session markerは削除せずprivate証拠へ保全し、元OBS設定選択を戻した。

2動画のdecodeではclipping0、10ms RMS<=2の連続ほぼ無音0秒。
これは可聴品質PASSではない。endpoint全体のmixのため他アプリ音声混入は人による確認が必要。
字幕原点はOBSファイル生成を50ms pollingしており、約50msの時刻不確かさがある。
AORJ192ms問題の発生源はUNKNOWN／INCONCLUSIVEのままで、未再現を解決済み扱いしない。
動画、native音色・音程、SE停止／寿命、速度切替の違和感をHUMAN_REVIEW_REQUIREDとして残す。

## RC配布物と公開判定

Windows x64 core／bridge／zlib、完全な対応code/build/relink source、依存sourceとsupport、
LICENSE／NOTICE、Compatibility DB、manifest、SHA256SUMSを別出力へ作成する。
音声処理の実装変更はなく、既存の検証済みPhase9 final DLLを採用。
binary source commitはd451f54da8aea4217130574df42aee775cd369a5、配布source commitはmanifestで別記する。
ビルド・再リンク方法は[V04_RC_BUILD](V04_RC_BUILD.md)。checksum file自身とZIP hashは外部integrityへ記録。
sourceからnon-build private検証reportを除き、全compiled source／headers／build recipesは保持。
ROM／BIOS／save／state／private WAV／MP4はZIPへ含めない。

RCのローカル確認準備は可能。全タイトル完全対応は条件にしない。
既知SE短縮と6未確認identityを説明し、人によるBGM／動画確認を終えてから公開範囲を確定する。
未聴取項目をPASSへ昇格しない。重大新規異常が確認された場合は公開を保留する。
公開v0.3の3成果物SHA／clean worktreeと保護WAV127本を再確認し、削除・容量整理・push・tag・Releaseなし。

## 完成RC

ZIP: `build-phase10/rc/mgba-fixed-audio-tempo-v0.4-preview-rc1-win64.zip`

SHA256: `80896A479D5E5465C4A7C67D427E26B0DBF0955D6FC6874AA5738D60B4D85CFF`

配布source: `cd8a3562d8d3d2cffcdfd68ef18ecece6e39b32d`

{
  "passed": true,
  "license_materials": {
    "status": "PASS",
    "exit_code": 0
  },
  "recursive_public_audit": {
    "passed": true,
    "files": 1,
    "archives": 10,
    "archive_members": 7283,
    "git_blobs": 0,
    "git_objects": 0,
    "rom": "NONE",
    "save": "NONE",
    "proprietary_bios": "NONE",
    "unapproved_video": "NONE",
    "secret": "NONE",
    "private_path": "NONE"
  },
  "installation": {
    "status": "PASS",
    "exit_code": 0,
    "scope": "New isolated sandbox: collisions, repeated install, shared/modified files, invalid ownership record; original files preserved"
  },
  "relinking": {
    "status": "PASS",
    "exit_code": 0,
    "scope": "Only supplied source/support used; changed bridge loaded by unchanged core in runner and actual RetroArch; exact original AAMJ menu golden; speaker muted",
    "report_sha256": "FE8B165EAFCD4AF36B39FA8E2768AC30E863C58334F1DD5362BD4865A787C95F"
  },
  "package_integrity": {
    "status": "PASS",
    "files_covered": 73,
    "zip_sha256": "80896A479D5E5465C4A7C67D427E26B0DBF0955D6FC6874AA5738D60B4D85CFF",
    "checksum_file_sha256": "728FB91D1238D4F69E9FD20500A33D7E504462FF854E2EDFBCC9F81AA3E62E41"
  },
  "limitation": "Package-source commit precedes the final validation-report commit. This report is external to the immutable RC ZIP; product binaries remain unchanged."
}
