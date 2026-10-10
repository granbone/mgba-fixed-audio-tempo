"""Prove modified agbplay can be rebuilt and used by the unmodified Preview core.

Test mutations and logs stay in a new external directory, never in the package.
SPDX-License-Identifier: MPL-2.0
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from test_license_materials import validate_materials

MARKER = '[LGPL RELINK TEST] agbplay modified library active'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'tool-bin', 'rom', 'retroarch-dir', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--muted', action='store_true', help='Mute only the frontend speaker output; synthesis and PCM assertions remain enabled.')
    args = parser.parse_args()
    package, out, tool = args.package.resolve(), args.output_dir.resolve(), args.tool_bin.resolve()
    if out == package or package in out.parents:
        raise ValueError('Test output must be outside package')
    out.mkdir(parents=True, exist_ok=False)
    validate_materials(package, True)
    source_hashes = {p.name: digest(p) for p in (package / 'source').glob('*.zip')}
    for archive, folder in (('bridge-source', 'bridge'), ('agbplay-source', 'agbplay'),
                            ('mgba-preview-source', 'mgba'), ('relink-support', 'support')):
        with zipfile.ZipFile(package / f'source/{archive}.zip') as z:
            z.extractall(out / folder)
    target = out / 'agbplay/src/agbplay/MP2KContext.cpp'
    original_source = target.read_bytes()
    text = original_source.decode()
    needle = '    assert(playerTableInfo.size() <= 32);'
    assert text.count(needle) == 1
    text = ('// Private relinking test modified on 2026-10-03; not a release change.\n'
            '#include <cstdio>\n' + text.replace(
                needle, '    std::fputs("' + MARKER + '\\n", stderr);\n' + needle, 1))
    target.write_text(text, encoding='utf-8')
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('MGBA_', 'CMAKE_', 'PKG_CONFIG')) and
           k.upper() not in ('CPATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'INCLUDE', 'LIB')}
    windows = Path(env.get('SystemRoot', 'C:/Windows'))
    env['PATH'] = os.pathsep.join(map(str, (tool, windows / 'System32', windows)))
    report = ['PRIVATE MODIFIED-LIBRARY RELINK TEST',
              'Inputs: extracted Preview source/support archives; unmodified general-purpose tools.',
              'No original checkout/build tree or installed fmt/Boost/libzip development packages used.']

    def run(command, name, runtime_env=None, cwd=None):
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        with (out / f'{name}.txt').open('wb') as log:
            subprocess.run(list(map(str, command)), env=runtime_env or env, cwd=cwd,
                           stdout=log, stderr=subprocess.STDOUT, check=True,
                           startupinfo=startup,
                           timeout=60 if name == 'retroarch-load' else None,
                           creationflags=subprocess.CREATE_NO_WINDOW)

    run([tool / 'cmake.exe', '-S', out / 'bridge', '-B', out / 'build', '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_CXX_COMPILER={tool}/g++.exe',
         f'-DCMAKE_MAKE_PROGRAM={tool}/ninja.exe', f'-DAGBPLAY_SOURCE_DIR={out}/agbplay',
         '-DMP2K_PORTABLE_STATIC_DEPS=ON', f'-DMP2K_RELINK_SUPPORT_DIR={out}/support'], 'configure')
    run([tool / 'cmake.exe', '--build', out / 'build', '--parallel', '4'], 'build')
    deps = subprocess.check_output([str(tool / 'ninja.exe'), '-C', str(out / 'build'), '-t', 'deps'],
                                   env=env, text=True)
    forbidden_includes = []
    for line in deps.splitlines():
        if not line.startswith('    '):
            continue
        path = Path(line.strip()).resolve()
        assert path.is_relative_to(out) or path.is_relative_to(tool.parent), path
        if path.is_relative_to(tool.parent / 'include'):
            relative = path.relative_to(tool.parent / 'include').as_posix()
            if relative.startswith(('boost/', 'fmt/')) or relative in {'zip.h', 'zipconf.h', 'zlib.h', 'zconf.h'}:
                forbidden_includes.append(relative)
    assert not forbidden_includes, forbidden_includes
    report.append('Modified agbplay + bridge rebuild PASS; all observed non-system headers supplied.')
    runner = out / 'relink-runner.exe'
    run([tool / 'gcc.exe', '-std=c11', '-O2', '-static',
         '-I' + str(out / 'mgba/src/platform/libretro'),
         out / 'mgba/tools/mp2k-audio-trace/retro-runner.c', '-o', runner], 'runner-build')

    retro = out / 'RetroArch test'
    retro.mkdir()
    shutil.copy2(args.retroarch_dir / 'retroarch.exe', retro / 'retroarch.exe')
    for p in args.retroarch_dir.glob('*.dll'):
        if 'agbplay' not in p.name.lower() and 'mp2k' not in p.name.lower():
            shutil.copy2(p, retro / p.name)
    run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
         package / 'install_to_retroarch.ps1', '-RetroArchPath', retro], 'install')
    installed = retro / 'cores/libmgba_mp2k_bridge.dll'
    original_binary = installed.read_bytes()
    rebuilt = out / 'build/libmgba_mp2k_bridge.dll'
    shutil.copy2(rebuilt, installed)
    assert digest(installed) == digest(rebuilt) != hashlib.sha256(original_binary).hexdigest().upper()
    core = retro / 'cores/mgba_fixed_audio_libretro.dll'
    assert digest(core) == digest(package / 'cores/mgba_fixed_audio_libretro.dll')
    runtime_env = dict(env, PATH=os.pathsep.join(map(str, (windows / 'System32', windows))),
                       MGBA_FIXED_AUDIO_TEMPO='1', MGBA_MP2K_BRIDGE_PATH=str(installed),
                       MGBA_FIXED_AUDIO_RUN_TRACE='1',
                       MGBA_FIXED_AUDIO_OUTPUT_PATH=str(out / 'modified-callback.s16le'),
                       LIBRETRO_SYSTEM_DIRECTORY=str(out))
    run([runner, core, args.rom, out / 'modified', 'normal', '1100',
         out / 'mgba/tools/mp2k-audio-trace/menu-sequence.txt'], 'core-load', runtime_env)
    assert MARKER in (out / 'core-load.txt').read_text(errors='replace')
    trace = (out / 'modified.log').read_text(errors='replace')
    assert 'FIXED AUDIO ACTIVE' in trace and 'bridge=loaded' in trace
    assert 'underrun=0 overrun=0 fallback=0' in trace
    assert digest(out / 'modified-callback.s16le') == '365E40316EF8FA01C45B413E53600DEBE55CABBFB5172A9237981C022EFC408A'
    report.append('Installed modified bridge loaded by unchanged Preview core PASS; identifier observed.')
    report.append('AAMJ menu callback PCM equals human-validated golden SHA-256 with modified library.')
    config = retro / 'retroarch.cfg'
    config.write_text('video_driver = "d3d11"\naudio_driver = "xaudio"\n'
                      'video_vsync = "true"\naudio_sync = "true"\n'
                      'pause_nonactive = "false"\n'
                      'config_save_on_exit = "false"\n'
                      'log_verbosity = "true"\n' + ('audio_mute_enable = "true"\n' if args.muted else ''), encoding='utf-8')
    runtime_env.pop('MGBA_FIXED_AUDIO_OUTPUT_PATH')
    run([retro / 'retroarch.exe', '--verbose', '--max-frames=500',
         '--log-file=' + str(out / 'retroarch.log'), '-L', core, args.rom],
        'retroarch-load', runtime_env, retro)
    process_text = (out / 'retroarch-load.txt').read_text(errors='replace')
    retro_log = (out / 'retroarch.log').read_text(errors='replace')
    assert MARKER in process_text + retro_log
    assert 'FIXED AUDIO ACTIVE' in retro_log and 'bridge=loaded' in retro_log
    assert 'underrun=0 overrun=0 fallback=0' in retro_log
    report.append('Actual RetroArch loaded modified library PASS (D3D11/XAudio2, --max-frames=500, no fallback).')
    installed.write_bytes(original_binary)
    run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
         package / 'uninstall_from_retroarch.ps1', '-RetroArchPath', retro], 'uninstall')
    assert source_hashes == {p.name: digest(p) for p in (package / 'source').glob('*.zip')}
    assert MARKER.encode() not in original_source
    report.append('Published source archives unchanged; private test mutation not distributed.')
    report.append('Modified bridge SHA-256: ' + digest(rebuilt))
    (out / 'license-relink-validation.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
    print('\n'.join(report))
    print(out / 'license-relink-validation.txt')


if __name__ == '__main__':
    main()
