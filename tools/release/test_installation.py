"""Exercise installer refusal, ownership and uninstall preservation in a sandbox.

SPDX-License-Identifier: MPL-2.0
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    sandbox = Path(tempfile.mkdtemp(prefix='installer-', dir=args.output_dir))

    def make(name):
        root = sandbox / name
        (root / 'cores').mkdir(parents=True)
        (root / 'saves').mkdir()
        for path in ('retroarch.exe', 'retroarch.cfg', 'cores/mgba_libretro.dll', 'saves/example.srm'):
            (root / path).write_bytes(b'preserve original')
        return root

    def run(script, root, success=True, parameter='-RetroArchPath'):
        result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                 str(args.package / script), parameter, str(root)],
                                capture_output=True, text=True)
        assert (result.returncode == 0) == success, (script, result.stdout, result.stderr)

    def preserved(root):
        for path in ('retroarch.exe', 'retroarch.cfg', 'cores/mgba_libretro.dll', 'saves/example.srm'):
            assert (root / path).read_bytes() == b'preserve original', path

    root = make('normal with spaces')
    run('install_to_retroarch.ps1', root)
    assert (root / 'cores/libmgba_mp2k_bridge.dll').read_bytes() == (args.package / 'runtime/libmgba_mp2k_bridge.dll').read_bytes()
    run('install_to_retroarch.ps1', root, False)
    preserved(root)
    run('uninstall_from_retroarch.ps1', root)
    preserved(root)
    assert not (root / 'cores/mgba_fixed_audio_libretro.dll').exists()
    assert not (root / '.fixed-audio-preview-install.json').exists()
    assert not (root / 'cores/libmgba_mp2k_bridge.dll').exists()

    root = make('collision')
    custom = root / 'cores/mgba_fixed_audio_libretro.dll'
    custom.write_bytes(b'preexisting different custom core')
    run('install_to_retroarch.ps1', root, False)
    assert custom.read_bytes() == b'preexisting different custom core'
    preserved(root)
    assert not (root / 'cores/libmgba_mp2k_bridge.dll').exists()

    root = make('shared-file')
    custom = root / 'cores/mgba_fixed_audio_libretro.dll'
    custom.write_bytes((args.package / 'cores/mgba_fixed_audio_libretro.dll').read_bytes())
    run('install_to_retroarch.ps1', root)
    run('uninstall_from_retroarch.ps1', root)
    assert custom.is_file(), 'preexisting identical core must be preserved'
    preserved(root)

    root = make('bridge-collision')
    bridge = root / 'cores/libmgba_mp2k_bridge.dll'
    bridge.write_bytes(b'preexisting different bridge')
    run('install_to_retroarch.ps1', root, False)
    assert bridge.read_bytes() == b'preexisting different bridge'
    assert not (root / 'cores/mgba_fixed_audio_libretro.dll').exists()
    preserved(root)

    root = make('shared-bridge')
    bridge = root / 'cores/libmgba_mp2k_bridge.dll'
    bridge.write_bytes((args.package / 'runtime/libmgba_mp2k_bridge.dll').read_bytes())
    run('install_to_retroarch.ps1', root, parameter='-RetroArchDir')
    run('uninstall_from_retroarch.ps1', root, parameter='-RetroArchDir')
    assert bridge.is_file(), 'preexisting identical bridge must be preserved'
    preserved(root)

    root = make('legacy-layout')
    run('install_to_retroarch.ps1', root)
    legacy = root / 'fixed-audio-preview-runtime/libmgba_mp2k_bridge.dll'
    legacy.parent.mkdir()
    (root / 'cores/libmgba_mp2k_bridge.dll').rename(legacy)
    record = root / '.fixed-audio-preview-install.json'
    data = json.loads(record.read_text(encoding='utf-8-sig'))
    for item in data['files']:
        if item['path'] == 'cores/libmgba_mp2k_bridge.dll':
            item['path'] = 'fixed-audio-preview-runtime/libmgba_mp2k_bridge.dll'
    record.write_text(json.dumps(data), encoding='utf-8')
    # An unrecorded core-sibling bridge must survive legacy uninstall.
    (root / 'cores/libmgba_mp2k_bridge.dll').write_bytes(b'unrecorded replacement')
    run('uninstall_from_retroarch.ps1', root)
    assert not legacy.exists()
    assert (root / 'cores/libmgba_mp2k_bridge.dll').read_bytes() == b'unrecorded replacement'
    preserved(root)

    root = make('modified')
    run('install_to_retroarch.ps1', root)
    custom = root / 'cores/mgba_fixed_audio_libretro.dll'
    custom.write_bytes(b'user replacement')
    run('uninstall_from_retroarch.ps1', root)
    assert custom.read_bytes() == b'user replacement'
    preserved(root)

    root = make('invalid-record')
    run('install_to_retroarch.ps1', root)
    record = root / '.fixed-audio-preview-install.json'
    data = json.loads(record.read_text(encoding='utf-8-sig'))
    data['files'][0]['path'] = '../victim.txt'
    record.write_text(json.dumps(data), encoding='utf-8')
    run('uninstall_from_retroarch.ps1', root, False)
    assert (root / 'cores/mgba_fixed_audio_libretro.dll').is_file(), 'invalid manifest must stop before deletion'
    preserved(root)
    print('Installation tests passed: core-sibling bridge layout, paths with spaces, legacy alias/layout, collisions, repeated install, shared/modified files, invalid record')
    print(sandbox)


if __name__ == '__main__':
    main()
