"""Prepare local final assets from immutable RC1 and an audited public source commit.

SPDX-License-Identifier: MPL-2.0. No Git push, tag, Release or upload operations.
"""
import argparse,datetime,hashlib,io,json,subprocess,zipfile
from pathlib import Path
from audit_public import inspect

EXPECTED_RC1='80896A479D5E5465C4A7C67D427E26B0DBF0955D6FC6874AA5738D60B4D85CFF'
def digest(data):return hashlib.sha256(data).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('source-root','rc1','validation','videos','output'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();root=a.source_root.resolve()
 def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
 assert not git('status','--porcelain').strip(),'Commit source and documentation before packaging'
 head=git('rev-parse','HEAD').decode().strip();tree=git('rev-parse','HEAD^{tree}').decode().strip()
 assert digest(a.rc1.read_bytes())==EXPECTED_RC1,'RC1 changed'
 a.output.mkdir(parents=True,exist_ok=False);package=a.output/'package';package.mkdir()
 def write(name,data):
  out=package/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
 def json_bytes(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode()
 with zipfile.ZipFile(a.rc1) as rc:
  old=json.loads(rc.read('manifest.json'));old_inventory=json.loads(rc.read('package-manifest.json'))
  for entry in old_inventory['files']:assert digest(rc.read(entry['path']))==entry['sha256'],entry['path']
  checksum=rc.read('SHA256SUMS.txt').decode().splitlines()
  covered=set()
  for line in checksum:
   h,name=line.split('  ',1);assert digest(rc.read(name))==h,name;covered.add(name)
  assert covered==set(rc.namelist())-{'SHA256SUMS.txt'},'Incomplete RC1 inventory'
  for name in rc.namelist():
   if name not in {'manifest.json','package-manifest.json','SHA256SUMS.txt','validation-summary.json','source/mgba-rc-distribution-source.zip'}:write(name,rc.read(name))
  matching=rc.read('source/mgba-preview-source.zip')
 # Keep matching compiled source intact, but remove historical non-build
 # evidence whose JSON-escaped paths were missed by the old RC1 auditor.
 private_helpers={'analyze_phase6_polling.py','probe_phase6_generic_lifetime.py','run_phase6_validation.py','test_gba_bgm_coverage_phase9.py','test_phase7_cross_title.py','test_phase7_regression.py','test_phase8_isolated.py','test_phase8_native.py','collect_gba_bgm_coverage_phase9.py'}
 sanitized=io.BytesIO();omitted=[]
 with zipfile.ZipFile(io.BytesIO(matching)) as original,zipfile.ZipFile(sanitized,'w',zipfile.ZIP_DEFLATED) as dst:
  for name in original.namelist():
   if name.endswith('/'):continue
   if name.startswith(('docs/three-x','docs/THREE_X_','docs/GBA_BGM_COVERAGE_PHASE9','docs/PRIVATE_AUDIO_RETENTION')) or (name.startswith('tools/public-integration/') and name.rsplit('/',1)[1] in private_helpers):omitted.append(name);continue
   data=original.read(name);inspect(name,data);dst.writestr(name,data)
 matching=sanitized.getvalue();write('source/mgba-preview-source.zip',matching)
 archive=git('archive','--format=zip','HEAD');clean=io.BytesIO()
 with zipfile.ZipFile(io.BytesIO(archive)) as source,zipfile.ZipFile(io.BytesIO(matching)) as binary_source,zipfile.ZipFile(clean,'w',zipfile.ZIP_DEFLATED) as dist:
  def compiled(name):return name.startswith(('src/','include/','tools/mp2k-audio-trace/bridge/')) or name in {'CMakeLists.txt','version.cmake'}
  names={n for n in source.namelist() if compiled(n) and not n.endswith('/')}
  expected={n for n in binary_source.namelist() if compiled(n) and not n.endswith('/')}
  assert names==expected,'Compiled source inventory differs from RC1'
  assert all(source.read(n)==binary_source.read(n) for n in names),'Compiled source bytes differ from tested DLL source'
  for name in source.namelist():
   if name.endswith('/') or name=='AGENTS.md' or name.startswith('source/'):continue
   dist.writestr(name,source.read(name))
   if name.startswith(('LICENSES/','compatibility/')) or name in {'README.md','README_JP.md','INSTALL.md','BUILDING.md','ARCHITECTURE.md','LICENSE','THIRD_PARTY_NOTICES.md','RELEASE_MANIFEST.md','COMPATIBILITY.md','docs/RELINKING.md','docs/LICENSE_AUDIT.md'} or name.startswith(('docs/V04_FINAL_RELEASE','docs/V04_RELEASE_NOTES','docs/V04_DEMO_MANIFEST','docs/V04_RELEASE_BUILD')):
    write(name,source.read(name))
  write('info/mgba_fixed_audio_libretro.info',source.read('tools/release/mgba_fixed_audio_libretro.info'))
 write('source/mgba-rc-distribution-source.zip',clean.getvalue())
 validation=json.loads(a.validation.read_text(encoding='utf8'));videos=json.loads((root/'docs/V04_DEMO_MANIFEST.json').read_text(encoding='utf8'))
 assert videos['video_user_review_pass'] and len(videos['videos'])==2
 for row in videos['videos']:
  assert digest((a.videos/row['filename']).read_bytes())==row['sha256']
 for name,h in old['actual_binary_sha256'].items():assert digest((package/name).read_bytes())==h
 assert validation['runtime_sha256']=={Path(n).name:h for n,h in old['actual_binary_sha256'].items()}
 meta=dict(version='v0.4-preview',publication='PREPARED_LOCALLY_NOT_PUBLISHED',source_commit=head,source_tree=tree,
  binary_source_commit=old['binary_source_commit'],actual_binary_sha256=old['actual_binary_sha256'],
  binary_provenance='Unchanged RC1 runtime; matching complete compiled source archive retained. Clean public source/distribution commit recorded separately.',
  agbplay_commit=old['agbplay_commit'],bridge_source_note=old['bridge_source_note'],
  rc1_zip_sha256=EXPECTED_RC1,repackaged=True,compiled_source_members_verified=len(names),
  source_archive_scope='All product code, headers, build/relink recipes and dependency materials included. Private non-build historical reports/evidence and local path-specific helpers are excluded from public source. No private Git history imported.',
  matching_source_nonbuild_omissions=omitted,
  video_user_review_pass=True,human_review_scope=videos['scope'],full_playthrough_claimed=False,
  remaining_human_review=validation['human_review_required'],packaged_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
 write('manifest.json',json_bytes(meta));write('validation-summary.json',json_bytes(validation))
 files=[dict(path=f.relative_to(package).as_posix(),sha256=digest(f.read_bytes()),size=f.stat().st_size) for f in sorted(package.rglob('*')) if f.is_file()]
 write('package-manifest.json',json_bytes(dict(product='mGBA Fixed Audio Tempo',version=meta['version'],sourceCommit=head,sourceTree=tree,binarySourceCommit=meta['binary_source_commit'],agbplayCommit=meta['agbplay_commit'],files=files)))
 write('SHA256SUMS.txt',''.join(digest(f.read_bytes())+'  '+f.relative_to(package).as_posix()+'\n' for f in sorted(package.rglob('*')) if f.is_file()).encode())
 final=a.output/'mgba-fixed-audio-tempo-v0.4-preview-win64.zip'
 with zipfile.ZipFile(final,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  for f in sorted(package.rglob('*')):
   if f.is_file():z.write(f,f.relative_to(package).as_posix())
 assets=[dict(filename=final.name,sha256=digest(final.read_bytes()),bytes=final.stat().st_size),*[dict(filename=x['filename'],sha256=x['sha256'],bytes=x['bytes']) for x in videos['videos']]]
 (a.output/'SHA256SUMS.txt').write_text(''.join(x['sha256']+'  '+x['filename']+'\n' for x in assets),encoding='ascii')
 assets.append(dict(filename='SHA256SUMS.txt',sha256=digest((a.output/'SHA256SUMS.txt').read_bytes()),bytes=(a.output/'SHA256SUMS.txt').stat().st_size))
 result=dict(**meta,zip_filename=final.name,zip_sha256=digest(final.read_bytes()),zip_bytes=final.stat().st_size,
  assets=assets,video_directory='Use the unchanged adopted demo files described by docs/V04_DEMO_MANIFEST.json',
  package_checksum_file_sha256=digest((package/'SHA256SUMS.txt').read_bytes()))
 (a.output/'release-integrity.json').write_bytes(json_bytes(result));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
