"""Package the tested Windows x64 v0.3 runtime with exact clean public source.
SPDX-License-Identifier: MPL-2.0. No upload; output must be new.
"""
import argparse,datetime,hashlib,io,json,subprocess,zipfile
from pathlib import Path
PRODUCTION_CONTENT='1DE978D3E02624657D1287370130451C2F878364DCEF65EB83EA24A3DC454F5B'
EXPECTED={'mgba_fixed_audio_libretro.dll':'A4BD66BA6A50CFC59109971DC1937BD3675991EE5E302FD3C1554F93C114BBF1','libmgba_mp2k_bridge.dll':'6C3EFC3F5FEDA53052A4432322A62357925842B79C54396479FFFCE857D29A40','zlib1.dll':'93E9243A44C29200EEACAF9658EFE2558581770E4B11CA4B500E18E424A6E3B5'}
def digest(b):return hashlib.sha256(b).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('root','runtime','output','validation'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();root=a.root.resolve()
 def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
 assert not git('status','--porcelain').strip(),'Source must be clean and committed'
 head=git('rev-parse','HEAD').decode().strip();tree=git('rev-parse','HEAD^{tree}').decode().strip()
 source=git('archive','--format=zip','HEAD')
 validation=json.loads(a.validation.read_text(encoding='utf-8'));assert validation['passed'] and validation['runtime_sha256']==EXPECTED
 a.output.mkdir(parents=True,exist_ok=False);package=a.output/'package';package.mkdir()
 def write(n,b):
  dest=package/n;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b)
 with zipfile.ZipFile(io.BytesIO(source)) as z:
  h=hashlib.sha256()
  for n in sorted(z.namelist()):
   if n.endswith('/'):continue
   if n.startswith(('src/','include/')) or n in ('CMakeLists.txt','version.cmake'):h.update(n.encode()+b'\0'+z.read(n).replace(b'\r\n',b'\n')+b'\0')
   if n.startswith(('LICENSES/','docs/','compatibility/','.github/')) or n in ('README.md','README_JP.md','INSTALL.md','README_UPSTREAM.md','COMPATIBILITY.md','BUILDING.md','ARCHITECTURE.md','LICENSE','THIRD_PARTY_NOTICES.md','RELEASE_MANIFEST.md'):write(n,z.read(n))
  assert h.hexdigest().upper()==PRODUCTION_CONTENT
  for n in ('install_to_retroarch.ps1','uninstall_from_retroarch.ps1','launch_fixed_audio.ps1'):write(n,z.read('tools/release/'+n))
  write('info/mgba_fixed_audio_libretro.info',z.read('tools/release/mgba_fixed_audio_libretro.info'))
  bridge=io.BytesIO()
  with zipfile.ZipFile(bridge,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as bz:
   for n in z.namelist():
    if n.startswith('tools/mp2k-audio-trace/bridge/') and not n.endswith('/'):bz.writestr(n.rsplit('/',1)[1],z.read(n))
  write('source/bridge-source.zip',bridge.getvalue())
 write('source/mgba-preview-source.zip',source)
 for n in ('agbplay-source.zip','relink-support.zip','dependency-source.zip'):write('source/'+n,(root/'source'/n).read_bytes())
 binaries={}
 for n,d in [('mgba_fixed_audio_libretro.dll','cores'),('libmgba_mp2k_bridge.dll','runtime'),('zlib1.dll','runtime')]:
  b=(a.runtime/n).read_bytes();sha=digest(b);assert sha==EXPECTED[n];write(d+'/'+n,b);binaries[d+'/'+n]=sha
 write('BINARY_CHECKSUMS.txt',''.join(v+'  '+n+'\n' for n,v in binaries.items()).encode())
 metadata=dict(version='v0.3-preview',artifact_kind='Windows x64 experimental prerelease',source_commit=head,source_tree=tree,
  production_source_content_sha256=PRODUCTION_CONTENT,packaged_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
  actual_binary_sha256=binaries,binary_provenance='Exact freshly built and tested v0.3 RC bytes, identical to the two demonstration recordings; not the v0.2 core/bridge.',
  embedded_version_note='RC Git-derived version metadata references former ancestry; source rebuild metadata may differ. Old Git objects/tags are not distributed.',
  experimental_default=True,three_modes_one_dll=True,full_playthrough_claimed=False,video_content='USER_ADOPTED',video_rights='USER_CONFIRMED; not independently certified',
  agbplay_commit='0b87da48d2502da359e45718eec8566ac40fa9d7')
 write('manifest.json',(json.dumps(metadata,indent=2)+'\n').encode());write('validation-summary.json',(json.dumps(validation,indent=2)+'\n').encode())
 files=[dict(path=f.relative_to(package).as_posix(),sha256=digest(f.read_bytes()),size=f.stat().st_size) for f in sorted(package.rglob('*')) if f.is_file()]
 write('package-manifest.json',(json.dumps(dict(product='mGBA Fixed Audio Tempo',version='v0.3-preview',sourceCommit=head,sourceTree=tree,agbplayCommit=metadata['agbplay_commit'],files=files),indent=2)+'\n').encode())
 write('SHA256SUMS.txt',''.join(digest(f.read_bytes())+'  '+f.relative_to(package).as_posix()+'\n' for f in sorted(package.rglob('*')) if f.is_file()).encode())
 archive=a.output/'mgba-fixed-audio-tempo-v0.3-preview-win64.zip'
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  for f in sorted(package.rglob('*')):
   if f.is_file():z.writestr(f.relative_to(package).as_posix(),f.read_bytes())
 result=dict(file=archive.name,sha256=digest(archive.read_bytes()),bytes=archive.stat().st_size,**metadata)
 (a.output/'release-integrity.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
