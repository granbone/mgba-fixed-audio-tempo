"""Audit public files, ZIP members and every reachable Git blob. MPL-2.0.

This supplements source review: file signatures cannot prove ownership.
No credentials, game data or extracted archive members are written by this tool.
"""
import argparse, hashlib, io, json, re, subprocess, zipfile, zlib
from pathlib import Path, PurePosixPath

FORBIDDEN = {'.gb','.gbc','.gba','.sav','.srm','.state','.rawstate','.ss0','.bin',
             '.mp4','.mov','.wav','.pcm','.s16le','.7z','.rar','.gz','.xz','.bz2'}
SECRET = re.compile(rb'github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9_-]{30,}|AKIA[0-9A-Z]{16}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
PRIVATE = re.compile(rb'(?:J:[\\/]+[A-Za-z0-9_. -]{4}|[DT]:[\\/]+(?:RetroRom|RetroGameRoms)|[A-Za-z]:[\\/]+Users[\\/]+|\\\\+AS[0-9]+[\\/]+)',re.I)
# Nintendo logo header patterns detect renamed/embedded cartridge images.
GB_LOGO = bytes.fromhex('CEED6666CC0D000B03730083000C000D0008111F8889000EDCCC6EE6DDDDD999BBBB67636E0EECCCDDDC999FBBB9333E')
GBA_LOGO = bytes.fromhex('24FFAE51699AA2213D84820A84E409AD11248B98C0817F21A352BE199309CE2010464A4AF82731EC58C7E83382E3CEBF')
stats = dict(files=0,archives=0,archive_members=0,git_blobs=0,git_objects=0)

def inspect(name,data,depth=0):
    p=PurePosixPath(name)
    assert depth<12,('archive nesting',name)
    assert not p.is_absolute() and '..' not in p.parts and not re.match(r'^[A-Za-z]:',name),('unsafe name',name)
    assert '.git' not in p.parts,('Git history embedded in public artifact',name)
    assert p.suffix.lower() not in FORBIDDEN,('prohibited extension',name)
    if zipfile.is_zipfile(io.BytesIO(data)):
        stats['archives']+=1
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            assert z.testzip() is None,('corrupt archive',name)
            for item in z.infolist():
                if item.is_dir():continue
                stats['archive_members']+=1
                inspect(item.filename,z.read(item),depth+1)
        return
    assert not data.startswith((b'7z\xbc\xaf\x27\x1c',b'Rar!',b'RASTATE',b'\x1f\x8b')),('unapproved archive/state signature',name)
    for logo,offset in ((GB_LOGO,0x104),(GBA_LOGO,4)):
        start=data.find(logo)
        # A short logo constant in emulator source/object code is not a game;
        # a full cartridge header at its ROM offset is rejected.
        assert start!=offset,('cartridge header signature',name)
        if start>=offset and len(data)>=start-offset+32768:
            base=start-offset
            if logo==GB_LOGO:
                header=data[base:base+0x150]
                checksum=0
                for value in header[0x134:0x14d]:checksum=(checksum-value-1)&255
                assert len(header)<0x150 or checksum!=header[0x14d],('embedded GB header',name)
            else:
                header=data[base:base+0xc0]
                assert len(header)<0xc0 or header[0xb2]!=0x96,('embedded GBA header',name)
    if data.startswith(b'%PDF'):
        for block in re.findall(rb'stream\r?\n(.*?)\r?\nendstream',data,re.S):
            try:expanded=zlib.decompress(block)
            except zlib.error:continue
            assert not SECRET.search(expanded) and not PRIVATE.search(expanded),('PDF private content',name)
    assert not SECRET.search(data),('credential signature',name)
    # Binary debug and UTF-16 strings are inspected as well as UTF-8 text.
    assert not PRIVATE.search(data) and not PRIVATE.search(data.replace(b'\x00',b'')),('personal path',name)
    # JSON can also encode the colon/backslash as Unicode escapes. Inspect
    # decoded keys/values, not just the serialized representation.
    if p.suffix.lower()=='.json':
        try:document=json.loads(data)
        except (ValueError,UnicodeDecodeError):document=None
        def strings(value):
            if isinstance(value,str):yield value
            elif isinstance(value,dict):
                for key,item in value.items():yield key;yield from strings(item)
            elif isinstance(value,list):
                for item in value:yield from strings(item)
        for value in strings(document):
            encoded=value.encode('utf8')
            assert not PRIVATE.search(encoded) and not SECRET.search(encoded),('decoded JSON private content',name)

def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--history',action='store_true')
    p.add_argument('--tracked-only',action='store_true',help='Audit the tracked working tree, excluding private ignored build/test evidence')
    p.add_argument('--allowed-root',action='append',default=[],help='Exact pre-audited roots authorized for safe history integration')
    p.add_argument('--report',type=Path)
    a=p.parse_args();root=a.root.resolve()
    if root.is_file():inspect(root.name,root.read_bytes());stats['files']=1
    else:
        paths = [root/name for name in git(root,'ls-files','-z').decode().split('\0') if name] if a.tracked_only else sorted(root.rglob('*'))
        for path in paths:
            if not path.is_file() or '.git' in path.relative_to(root).parts:continue
            assert not path.is_symlink(),('symlink',str(path))
            inspect(path.relative_to(root).as_posix(),path.read_bytes());stats['files']+=1
    if a.history:
        objects=git(root,'rev-list','--objects','--all').decode().splitlines()
        names={line.split(' ',1)[0]:line.partition(' ')[2] for line in objects}
        inventory=git(root,'cat-file','--batch-all-objects','--batch-check=%(objectname) %(objecttype)').decode().splitlines()
        stats['git_objects']=len(inventory)
        assert set(names)<={line.split()[0] for line in inventory},'Referenced objects missing from inventory'
        extras=[line.split() for line in inventory if line.split()[0] not in names]
        stats['unreachable_objects_audited']=len(extras)
        # Merge conflict resolution can leave unreachable trees/blobs. Inspect
        # them as well; none are silently excluded by reachability.
        for oid,kind in extras:
            names.setdefault(oid,'')
            if kind=='tree':
                for entry in git(root,'ls-tree','-r',oid).decode().splitlines():
                    info,path=entry.split('\t',1)
                    inspect(path,b'')
                    child=info.split()[2]
                    if child in names and not names[child]:names[child]=path
        objects=[oid+(' '+name if name else '') for oid,name in names.items()]
        payload=subprocess.check_output(['git','-C',str(root),'cat-file','--batch'],
                                        input=('\n'.join(names)+'\n').encode())
        pos=0
        for line in objects:
            oid,_,name=line.partition(' ')
            end=payload.index(b'\n',pos)
            found,kind,length=payload[pos:end].decode().split()
            assert found==oid,'Git batch order drift'
            length=int(length);data=payload[end+1:end+1+length];pos=end+2+length
            inspect(name or oid,data)
            if kind=='blob':stats['git_blobs']+=1
        assert not git(root,'tag','--list','v0.1-preview').strip(),'old tag must not be imported'
        roots=set(git(root,'rev-list','--max-parents=0','--all').decode().splitlines())
        if a.allowed_root:
            assert all(re.fullmatch(r'[0-9a-f]{40}',v) for v in a.allowed_root),'invalid authorized root'
            assert roots==set(a.allowed_root),'unapproved history root'
        else:
            assert len(roots)==1,'exactly one clean root'
        stats['audited_roots']=sorted(roots)
    result=dict(passed=True,**stats,rom='NONE',save='NONE',proprietary_bios='NONE',unapproved_video='NONE',secret='NONE',private_path='NONE')
    if a.report:a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
