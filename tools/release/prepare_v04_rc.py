"""Local-only v0.4 RC, retaining the exact Phase9 tested runtime and matching source.
SPDX-License-Identifier: MPL-2.0. No network/publication operations.
"""
import argparse,datetime,hashlib,io,json,subprocess,zipfile
from pathlib import Path
def sha(b):return hashlib.sha256(b).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('root','runtime','runtime-manifest','validation','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();root=a.root.resolve()
 def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
 assert not git('status','--porcelain').strip(),'Commit reviewable distribution sources first'
 head=git('rev-parse','HEAD').decode().strip();tree=git('rev-parse','HEAD^{tree}').decode().strip()
 runtime=json.loads(a.runtime_manifest.read_text(encoding='utf8'));binary_head=runtime['source_head']
 assert runtime['source_clean'] and len(binary_head)==40
 expected={Path(f['path']).name:f['sha256'] for f in runtime['files']}
 assert set(expected)=={'mgba_fixed_audio_libretro.dll','libmgba_mp2k_bridge.dll','zlib1.dll'}
 # Distribution changes must not change anything compiled into this runtime.
 assert not git('diff',binary_head,head,'--','src','include','CMakeLists.txt','version.cmake','tools/mp2k-audio-trace/bridge').strip()
 validation=json.loads(a.validation.read_text(encoding='utf8'));assert validation['runtime_sha256']==expected
 a.output.mkdir(parents=True,exist_ok=False);package=a.output/'package';package.mkdir()
 def write(name,b):
  f=package/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
 def source_archive(commit):
  original=git('archive','--format=zip',commit);filtered=io.BytesIO()
  # Historical private validation reports are not build inputs. Keep all source,
  # headers, CMake/build/relink recipes, licenses and dependency material intact.
  with zipfile.ZipFile(io.BytesIO(original)) as z,zipfile.ZipFile(filtered,'w',zipfile.ZIP_DEFLATED) as dst:
   private_helpers={'analyze_phase6_polling.py','probe_phase6_generic_lifetime.py','run_phase6_validation.py','test_gba_bgm_coverage_phase9.py','test_phase7_cross_title.py','test_phase7_regression.py','test_phase8_isolated.py','test_phase8_native.py'}
   for n in z.namelist():
    if n=='AGENTS.md' or n.startswith(('docs/THREE_X_','docs/GBA_BGM_COVERAGE_PHASE9','docs/PRIVATE_AUDIO_RETENTION','source/')):continue
    if n.startswith('tools/public-integration/') and n.rsplit('/',1)[1] in private_helpers:continue
    if not n.endswith('/'):dst.writestr(n,z.read(n))
  return filtered.getvalue()
 current=source_archive(head);matching=source_archive(binary_head)
 with zipfile.ZipFile(io.BytesIO(current)) as z:
  for n in z.namelist():
   if n.endswith('/'):continue
   if n.startswith(('LICENSES/','compatibility/')) or n in ('README.md','README_JP.md','INSTALL.md','BUILDING.md','ARCHITECTURE.md','LICENSE','THIRD_PARTY_NOTICES.md','RELEASE_MANIFEST.md','COMPATIBILITY.md','docs/RELINKING.md','docs/LICENSE_AUDIT.md','docs/V04_RC_VALIDATION.md','docs/V04_RC_VALIDATION.json','docs/V04_RC_BUILD.md'):
    write(n,z.read(n))
  for n in ('install_to_retroarch.ps1','uninstall_from_retroarch.ps1','launch_fixed_audio.ps1'):write(n,z.read('tools/release/'+n))
  write('info/mgba_fixed_audio_libretro.info',z.read('tools/release/mgba_fixed_audio_libretro.info'))
 with zipfile.ZipFile(io.BytesIO(matching)) as z:
  bridge=io.BytesIO()
  with zipfile.ZipFile(bridge,'w',zipfile.ZIP_DEFLATED) as b:
   for n in z.namelist():
    if n.startswith('tools/mp2k-audio-trace/bridge/') and not n.endswith('/'):b.writestr(n.rsplit('/',1)[1],z.read(n))
  write('source/bridge-source.zip',bridge.getvalue())
 write('source/mgba-preview-source.zip',matching);write('source/mgba-rc-distribution-source.zip',current)
 for n in ('agbplay-source.zip','relink-support.zip','dependency-source.zip'):write('source/'+n,(root/'source'/n).read_bytes())
 binaries={}
 for n,folder in [('mgba_fixed_audio_libretro.dll','cores'),('libmgba_mp2k_bridge.dll','runtime'),('zlib1.dll','runtime')]:
  b=(a.runtime/n).read_bytes();assert sha(b)==expected[n];write(folder+'/'+n,b);binaries[folder+'/'+n]=sha(b)
 write('BINARY_CHECKSUMS.txt',''.join(h+'  '+n+'\n' for n,h in binaries.items()).encode())
 meta=dict(version='v0.4-preview-rc1',public=False,source_commit=head,source_tree=tree,binary_source_commit=binary_head,
  binary_provenance='Exact Phase9 final runtime, no DSP/profile/STOP changes in Phase10. Matching binary-source archive and complete RC distribution-source archive both included.',
  actual_binary_sha256=binaries,agbplay_commit='0b87da48d2502da359e45718eec8566ac40fa9d7',
  bridge_source_note='Unmodified agbplay archive plus deterministic build-time SequenceReader patches in matching bridge CMakeLists and priority_order.hpp. Both supplied.',
  source_archive_scope='Complete corresponding product code/build/relink source. Non-build private Phase reports/retention instructions and8 local diagnostic helpers with private paths omitted. Third-party archives supplied separately without duplicate embedding. No compiled source is modified or omitted.',
  full_playthrough_claimed=False,human_review='HUMAN_REVIEW_REQUIRED',packaged_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
 write('manifest.json',(json.dumps(meta,indent=2)+'\n').encode());write('validation-summary.json',(json.dumps(validation,indent=2)+'\n').encode())
 files=[dict(path=f.relative_to(package).as_posix(),sha256=sha(f.read_bytes()),size=f.stat().st_size) for f in sorted(package.rglob('*')) if f.is_file()]
 write('package-manifest.json',(json.dumps(dict(product='mGBA Fixed Audio Tempo',version=meta['version'],sourceCommit=head,binarySourceCommit=binary_head,sourceTree=tree,agbplayCommit=meta['agbplay_commit'],files=files),indent=2)+'\n').encode())
 write('SHA256SUMS.txt',''.join(sha(f.read_bytes())+'  '+f.relative_to(package).as_posix()+'\n' for f in sorted(package.rglob('*')) if f.is_file()).encode())
 archive=a.output/'mgba-fixed-audio-tempo-v0.4-preview-rc1-win64.zip'
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  for f in sorted(package.rglob('*')):
   if f.is_file():z.write(f,f.relative_to(package).as_posix())
 # External integrity includes checksum file itself, avoiding a self-hash cycle.
 result=dict(zip=str(archive),zip_sha256=sha(archive.read_bytes()),zip_bytes=archive.stat().st_size,
  checksum_file_sha256=sha((package/'SHA256SUMS.txt').read_bytes()),**meta)
 (a.output/'release-integrity.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
