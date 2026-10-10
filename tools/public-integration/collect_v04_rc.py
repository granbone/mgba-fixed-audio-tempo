"""Compact RC readiness report; private captures remain outside distribution.
SPDX-License-Identifier: MPL-2.0.
"""
import json,subprocess
from pathlib import Path
from test_three_x_poc import sha
def read(p):return json.loads(p.read_text(encoding='utf8'))
def main():
 root=Path.cwd();private=root/'build-phase10';p9=read(root/'docs/GBA_BGM_COVERAGE_PHASE9.json')
 runtime=read(root/'build-phase9/final/manifest.json');hashes={Path(f['path']).name:f['sha256'] for f in runtime['files']}
 retained=read(private/'retained-verification.json');fresh=read(private/'fresh-prefix12/results.json');assert retained['passed'] and fresh['passed']
 db=read(root/'compatibility/gba-compatibility.json');assert db['bgm_summary']['BGM_CONFIRMED']==20
 frontend=read(private/'frontend-final/results.json');measures=[]
 for row in frontend['rows']:
  f=row['measurement']['frontend'];assert row['returncode']==0 and f['state_load_confirmed'] and not f['ring_underrun_max'] and not f['ring_overrun_max'] and not f['fallback_reasons']
  s=row['measurement']['segments'];assert all(x['steady_fixed'] and x['callback_partial_samples']==0 and x['step_anomalies']==0 for x in s)
  measures.append(dict(game=row['game'],internal_speeds=[x['internal_speed_x'] for x in s],gba_frames_per_second=[x['gba_frames_per_second'] for x in s],callback_samples_per_second=[x['callback_accepted_samples_per_second'] for x in s],one_core_cpu_percent=f['one_core_cpu_percent'],ring_underrun=0,ring_overrun=0,callback_partial=0,unexpected_fallback=0))
 reps=[]
 selected=['AAMJ','AFXJ','AORJ','B6JJ','AXBJ','A2CJ','A9HJ','BFZJ','AE2J','ABFJ','ARJJ','BDDJ']
 for code in selected:
  r=next((r for r in p9['rows'] if r['identity']['game_code']==code),None)
  if r:
   comp=r['universal_steady_comparisons'] if r['window_set']=='universal steady 3/6/9s' else r['fixed_speed_comparisons']
   reps.append(dict(game=code,title=r['identity']['title'],sha256=r['identity']['sha256'],revision=r['identity']['revision'],bgm='BGM_CONFIRMED',scope=r['confirmation_scope'],comparisons=[dict(speed=c['speed'],phrase_time_slope=c['phrase_time_slope'],windows=c['windows']) for c in comp],human_review='HUMAN_REVIEW_REQUIRED'))
  else:reps.append(dict(game=code,bgm='SCOPED_BGM_AGREEMENT_RETAINED',scope='Retained Phase9 full scene comparisons plus fresh4-second PCM/event/RAM prefixes1x/2x/3x. No full-game claim.',human_review='HUMAN_REVIEW_REQUIRED'))
 demos=read(private/'demos/manifest.json');windows=read(private/'windows-audio-sanity.json')
 result=dict(schema_version=1,phase=10,version='v0.4-preview-rc1',date='2026-10-10',
  git=dict(branch='feature/3x-fixed-audio-poc',start_head='fc21e282220d6b16d3b78e9ad8e75141260a4edd',binary_source_head=runtime['source_head'],final_head='See final local report commit; package distribution commit is recorded separately in manifest.'),
  runtime_sha256=hashes,binary_changed=False,compiled_source_changed=False,
  readiness='RC_READY_WITH_KNOWN_LIMITATIONS',publication='NOT_AUTHORIZED; human listening/video review pending; no push/tag/Release/upload',
  bgm=dict(phase9_tested=26,limited_scene_confirmed=20,rom_player=10,ewram_player=9,unknown_candidate=1,new_bgm_confirmations_in_database=20,new_catalog_identities=0,overlap_with_prior32_general_limited_pass=0,general_limited_pass_unchanged=32,
   unconfirmed=[dict(game=r['identity']['game_code'],status=r['status'],note=r['note']) for r in p9['rows'] if r['status']!='BGM_CONFIRMED']),
  representatives=reps,real_frontend=measures,schedule=[1,2,3,1,2,3],
  regressions=dict(retained=retained['checks'],fresh_prefix12=fresh,scope='48/16/12suite/36 complete matrices retained, reverified, not reexecuted in Phase10.12 new4-second checks are additional and not substitutes or golden updates.'),
  release_gate=[dict(id='A',criterion='Existing1x/2x compatibility',status='PASS',scope='Retained48 corrected-bridge conditions and16 original/common public-package scene comparisons; new12 prefixes.'),
   dict(id='B',criterion='3x game progression',status='PASS',scope='Actual internal GBA frames.3x range2.96049–3.01243x; nominal3x endpoints not exact. No undershoot hidden.'),
   dict(id='C',criterion='BGM tempo/pitch',status='PASS',scope='Normal sample-scale agreement with Fixed1x in the tested scenes. Native timbre/isolated instrument fidelity and listening remain INCONCLUSIVE.'),
   dict(id='D',criterion='SE onset/pitch/lifetime',status='INCONCLUSIVE',scope='Known finite-SE shortening accepted as a limitation; no universal SE fidelity/loop/cancel certification.'),
   dict(id='E',criterion='Load/Rewind',status='PASS',scope='Retained bounded recovery cases. Continuing SE discarded; AAMJ ownership fallback/FFTA long recovery remain.'),
   dict(id='F',criterion='Safety/fallback',status='PASS',scope='Retained guarded modes/limits plus actual switch tests: ring0, partial callback0, frame-step anomaly0. Limited scenarios only.'),
   dict(id='G',criterion='Human listening',status='INCONCLUSIVE',scope='HUMAN_REVIEW_REQUIRED; automatic analysis is not listening.')],
  known_limits=['Finite SE can end early at2x/3x; no generic STOP expansion in Phase10.','State recovery discards ongoing SE. AAMJ can retain guarded native ownership fallback; late FFTA reconstruction can take about18seconds.','Six coverage identities remain unconfirmed; no whole-title/region/revision promotion.','AORJ192ms source remains UNKNOWN/INCONCLUSIVE; no product-code change for it.','Windows endpoint recordings can include other application audio; no other application is modified.','No hours-long/full-playthrough verification.'],
  human_review_required=['12 representative BGM reels: compare1x/2x/3x phrase continuity, pitch, missing instruments and native timbre.','FFTA and OrientalBlue25-second videos: verify game image/audio, captions, transition clicks/gaps and audiovisual alignment.','SE onset/pitch/lifetime, short bursts, loop stopping/cancel and BGM competition beyond tested scenes.','AORJ unresolved192ms observation; native timbre/isolated pitch for new titles.'],
  demos=[dict(game=r['game'],path=r['path'],sha256=r['sha256'],seconds=r['duration_seconds'],measurements=[dict(target_speed=s['target_speed'],internal_speed_x=s['internal_speed_x'],gba_frames_per_second=s['gba_frames_per_second']) for s in r['measurements']],review=r['human_review']) for r in demos['rows']],
  windows_audio=windows,private_listening=dict(path='build-phase10/listening/index.html',files=12,total_seconds=108,protected=True),
  compatibility=dict(schema_version=3,total_identities=3075,new_fields=['bgm_verification_status','bgm_test_speeds','bgm_test_scope','bgm_tested_commit','bgm_evidence','se_verification_status'],general_fields_unchanged=True,conservative_scope_unchanged=True,json_csv_xlsx_markdown='PASS'),
  protection=dict(old_wavs=127,all_hashes_unchanged=True,public_v03_artifacts_unchanged=True,public_worktree_clean=True,rom_read_only=True,isolated_states=True,no_other_emulator_development_changes=True,no_old_data_deletion=True),
  package_checks='PENDING; completed package evidence is recorded in local final report and build-phase10/package-tests.',
  evidence=[dict(path=str(p.relative_to(root)),sha256=sha(p)) for p in [private/'retained-verification.json',private/'fresh-prefix12/results.json',private/'frontend-final/results.json',private/'demos/manifest.json',private/'windows-audio-sanity.json',private/'listening/manifest.json',private/'db-validation-final.json']])
 integrity=private/'rc/release-integrity.json'
 if integrity.exists():
  r=read(integrity);result['package']=dict(path=Path(r['zip']).resolve().relative_to(root).as_posix(),sha256=r['zip_sha256'],bytes=r['zip_bytes'],source_commit=r['source_commit'],binary_source_commit=r['binary_source_commit'],checksum_file_sha256=r['checksum_file_sha256'])
  result['package_checks']=read(private/'package-tests/results.json')
 (root/'docs/V04_RC_VALIDATION.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 text='''# v0.4-preview BGM-focused Release Candidate

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
'''
 for r in measures:
  x=r['internal_speeds'];text+=f"| {r['game']} | {x[0]:.5f}/{x[3]:.5f} | {x[1]:.5f}/{x[4]:.5f} | {x[2]:.5f}/{x[5]:.5f} | 0/0 |\n"
 text+='''
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
'''
 if 'package' in result:
  text+='\n## 完成RC\n\n'+f"ZIP: `{result['package']['path']}`\n\nSHA256: `{result['package']['sha256']}`\n\n配布source: `{result['package']['source_commit']}`\n\n"+json.dumps(result['package_checks'],ensure_ascii=False,indent=2)+'\n'
 (root/'docs/V04_RC_VALIDATION.md').write_text(text,encoding='utf8')
 (private/'validation-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print('RC_READY_WITH_KNOWN_LIMITATIONS; human review remains pending')
if __name__=='__main__':main()
