"""Fail release packaging if required source, licenses or relinking instructions are missing.

SPDX-License-Identifier: MPL-2.0
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import zipfile

REQUIRED = ('LICENSES/mGBA-MPL-2.0.txt', 'LICENSES/GPL-3.0.txt',
            'LICENSES/LGPL-3.0.txt', 'LICENSES/README.md', 'THIRD_PARTY_NOTICES.md',
            'docs/RELINKING.md', 'docs/LICENSE_AUDIT.md',
            'source/mgba-preview-source.zip', 'source/bridge-source.zip',
            'source/agbplay-source.zip', 'source/relink-support.zip',
            'source/dependency-source.zip', 'RELEASE_MANIFEST.md', 'BINARY_CHECKSUMS.txt')


def license_text(data):
    """Compare original text independent of CRLF and trailing blank lines."""
    return data.replace(b'\r\n', b'\n').rstrip() + b'\n'


def validate_materials(package, check_manifest=False):
    package = Path(package)
    for name in REQUIRED:
        assert (package / name).is_file(), ('missing compliance material', name)
    with zipfile.ZipFile(package / 'source/bridge-source.zip') as bridge, \
         zipfile.ZipFile(package / 'source/agbplay-source.zip') as agb, \
         zipfile.ZipFile(package / 'source/mgba-preview-source.zip') as core, \
         zipfile.ZipFile(package / 'source/relink-support.zip') as support, \
         zipfile.ZipFile(package / 'source/dependency-source.zip') as deps:
        assert hashlib.sha256(license_text(bridge.read('LICENSE'))).hexdigest() == 'da7eabb7bafdf7d3ae5e9f223aa5bdc1eece45ac569dc21b3b037520b4464768'
        assert hashlib.sha256(license_text(bridge.read('COPYING.GPL-3.0.txt'))).hexdigest() == '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903'
        assert license_text(bridge.read('LICENSE')) == license_text(agb.read('LICENSE')) == license_text((package / 'LICENSES/LGPL-3.0.txt').read_bytes())
        assert license_text(bridge.read('COPYING.GPL-3.0.txt')) == license_text((package / 'LICENSES/GPL-3.0.txt').read_bytes())
        assert license_text(core.read('LICENSE')) == license_text((package / 'LICENSES/mGBA-MPL-2.0.txt').read_bytes())
        for name in ('bridge.cpp', 'bridge.h', 'CMakeLists.txt'):
            data = bridge.read(name)
            assert b'SPDX-License-Identifier: LGPL-3.0-only' in data, name
            assert b'SPDX-License-Identifier: MPL' not in data, name
            assert data == core.read('tools/mp2k-audio-trace/bridge/' + name), name
        bridge.read('NOTICE.md')
        sources = re.search(r'set\(AGB_SOURCES\s+(.*?)\)', bridge.read('CMakeLists.txt').decode(), re.S)[1].split()
        for name in sources:
            data = agb.read('src/agbplay/' + name)
            assert b'[LGPL RELINK TEST]' not in data, 'test mutation leaked into distribution'
        assert len(sources) == 20, 'review the bridge source inventory after build changes'
        for name in ('fmt', 'zip', 'bz2', 'lzma', 'zstd', 'z'):
            support.read('lib/lib' + name + '.a')
        for name in ('zip.h', 'zipconf.h', 'zlib.h', 'zconf.h'):
            support.read('include/' + name)
        inventory = json.loads(deps.read('dependency-source-manifest.json'))
        assert {p['component'] for p in inventory['packages']} == {'fmt', 'libzip', 'bzip2', 'xz', 'zstd', 'zlib'}
        for component in ('fmt', 'libzip', 'bzip2', 'xz', 'zstd', 'zlib'):
            assert component + '/mingw-w64-' + component + '/PKGBUILD' in deps.namelist()
        for name in ('src/core/audio-clock.c', 'src/gba/mp2k-events.c',
                     'src/gba/mp2k-semantic.c', 'src/gba/mp2k-ownership.c', 'src/gba/mp2k-profile.c',
                     'include/mgba/core/audio-clock.h', 'include/mgba/internal/gba/mp2k-events.h',
                     'include/mgba/internal/gba/mp2k-semantic.h', 'include/mgba/internal/gba/mp2k-ownership.h',
                     'include/mgba/internal/gba/mp2k-profile.h'):
            assert b'Mozilla Public' in core.read(name), name
    if check_manifest:
        manifest = json.loads((package / 'package-manifest.json').read_text())
        for entry in manifest['files']:
            data = (package / entry['path']).read_bytes()
            assert hashlib.sha256(data).hexdigest().upper() == entry['sha256'], entry['path']
        assert manifest['agbplayCommit'] == '0b87da48d2502da359e45718eec8566ac40fa9d7'
    print('LICENSE MATERIALS PASS: original texts, source, build inputs, notices and relinking instructions')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    if args.package.is_file():
        # Archive inspection itself is performed by the release recursive auditor.
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            with zipfile.ZipFile(args.package) as archive:
                archive.extractall(folder)
            validate_materials(folder, True)
    else:
        validate_materials(args.package, True)


if __name__ == '__main__':
    main()
