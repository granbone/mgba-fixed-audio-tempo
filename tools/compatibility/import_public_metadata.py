"""Import text-only No-Intro metadata from a pinned libretro database revision.
SPDX-License-Identifier: MPL-2.0
No game binaries are requested. This is optional; builds use the saved snapshot.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from urllib.request import Request, urlopen

DAT_PATH = 'metadat/no-intro/Nintendo - Game Boy Advance.dat'
BASE = 'https://raw.githubusercontent.com/libretro/libretro-database/'


def read_url(url):
    with urlopen(Request(url, headers={'User-Agent': 'fixed-audio-metadata-import'}), timeout=60) as response:
        return response.read()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--revision', required=True, help='Full libretro database commit SHA')
    p.add_argument('--acquired', required=True, help='Actual acquisition date, YYYY-MM-DD')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    assert re.fullmatch('[0-9a-f]{40}', a.revision)
    url = BASE + a.revision + '/' + DAT_PATH.replace(' ', '%20')
    blob = read_url(url)
    text = blob.decode('utf-8-sig')
    assert text.startswith('clrmamepro ('), 'Expected textual metadata DAT'
    rows = []
    excluded = []
    # Each libretro clrmamepro game is flat except for its single-line rom entry.
    for block in re.findall(r'^game \(\n(.*?)^\)', text, re.M | re.S):
        def quoted(key):
            m = re.search(r'^\s*' + key + r' "([^"]*)"', block, re.M)
            return m[1] if m else None
        title = quoted('name')
        reason = re.search(r'\((?:Proto|Beta|Demo|Sample|Unl|Pirate|BIOS|Aftermarket|Test Program|Program|Debug|Multiboot)(?:[^)]*)\)', title, re.I)
        rom = re.search(r'rom \(.*?size (\d+) crc ([0-9a-f]+).*?sha1 ([0-9a-f]+)', block, re.I)
        serial = quoted('serial')
        # The upstream aggregate also includes unmarked homebrew/aftermarket.
        # Do not label those as official cartridge releases on naming alone.
        if not rom or reason or not serial or not re.fullmatch('[A-Z0-9]{4}', serial):
            excluded.append({'title': title, 'reason': reason[0] if reason else 'OFFICIAL_RELEASE_IDENTITY_NOT_ESTABLISHED'})
            continue
        groups = re.findall(r'\(([^)]*)\)', title)
        # Naming region is preserved as a set: a USA/Europe shared release is one identity.
        known_regions = {'Japan','USA','Europe','Australia','Korea','China','Taiwan','World','France','Germany','Italy','Spain','Netherlands','Sweden','Denmark','Brazil','Canada','United Kingdom','Russia'}
        region = next((g for g in groups if any(x.strip() in known_regions for x in g.split(','))), quoted('region') or 'UNKNOWN')
        languages = []
        for g in groups:
            if re.fullmatch(r'[A-Z][a-z](?:[,+][A-Z][a-z])*', g):
                languages = sorted(set(re.split('[,+]', g)))
                break
        revision = next((g for g in groups if re.match(r'^(Rev |v\d)', g)), None)
        # Absence of a revision tag is not evidence for header version 0.
        rows.append(dict(title=title, region=region, languages=languages, game_code=quoted('serial'), revision=revision,
                         crc32=rom[2].upper(), sha1=rom[3].upper(), sha256=None, rom_size=int(rom[1])))
    result = dict(source_id='LIBRETRO_NO_INTRO_GBA', url=url,
                  repository='https://github.com/libretro/libretro-database', revision=a.revision,
                  acquired=a.acquired, dat_sha256=hashlib.sha256(blob).hexdigest(),
                  dat_version=re.search(r'version "([^"]+)"', text)[1],
                  scope='Catalogued released identities with explicit four-character serials; labelled prototypes, demos, unlicensed, BIOS and test programs excluded. Entries without a serial require separate release verification and are omitted. Not a completeness or licensing guarantee.',
                  excluded=excluded, releases=rows)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('revision','acquired','dat_sha256','dat_version')}))
    print(f'Released metadata: {len(rows)}; excluded metadata: {len(excluded)}')


if __name__ == '__main__':
    main()
