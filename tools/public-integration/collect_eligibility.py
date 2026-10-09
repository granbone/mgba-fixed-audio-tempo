"""Probe exact supplied ROM hashes read-only. Never downloads game binaries.
SPDX-License-Identifier: MPL-2.0
Output contains hashes/validation outcomes, never paths or private filenames.
"""
import argparse, concurrent.futures, hashlib, json, os, subprocess
from pathlib import Path

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('rom-dir', 'scanner', 'probe', 'bridge', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    evidence = json.loads(a.scanner.read_text(encoding='utf-8'))
    index = {}
    for path in a.rom_dir.glob('*.gba'):
        # Header identity is only a search accelerator, never the match decision.
        with path.open('rb') as f:
            header = f.read(0xC0)
        if len(header) >= 0xC0:
            index.setdefault((header[0xAC:0xB0].decode('ascii', errors='replace'), path.stat().st_size), []).append(path)
    unique = {r['sha256']: r for r in evidence['records']
              if r['input_status']=='ROM_HEADER_VALID' and not r['scan_error'] and r['detected_driver']=='MP2K'}
    def run(r):
        row = dict(sha256=r['sha256'], sha1=r['sha1'], crc32=r['crc32'], rom_size=r['rom_size'], game_code=r['game_code'])
        for path in index.get((r['game_code'],r['rom_size']), []):
            with path.open('rb') as f:
                digest=hashlib.file_digest(f, 'sha256').hexdigest().upper()
            if digest != r['sha256']: continue
            try:
                result=subprocess.run([str(a.probe),str(a.bridge),str(path),r['crc32']],
                                      capture_output=True,text=True,timeout=90,check=True)
                return dict(row, **json.loads(result.stdout))
            except subprocess.TimeoutExpired:
                return dict(row, static_eligible=None, reason='PROBE_TIMEOUT')
            except (subprocess.CalledProcessError,ValueError):
                return dict(row, static_eligible=None, reason='PROBE_FAILURE')
        return dict(row, static_eligible=None, reason='EXACT_IDENTITY_NOT_AVAILABLE')
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        rows=list(pool.map(run, unique.values()))
    result=dict(schema_version=1, version_candidate='v0.3-preview',
                production_profile_sha256=hashlib.sha256((Path(__file__).resolve().parents[2]/'src/gba/mp2k-profile.c').read_bytes().replace(b'\r\n',b'\n')).hexdigest(),
                production_bridge_sha256=hashlib.sha256(a.bridge.read_bytes()).hexdigest(),
                scope='Production scanner/profile checks only. Live player/track and bridge rendering checks remain mandatory; no audio validation is inferred.',
                records=sorted(rows,key=lambda r:r['sha256']))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    from collections import Counter
    print(json.dumps(dict(total=len(rows),results=Counter(r['reason'] for r in rows))))
if __name__=='__main__': main()
