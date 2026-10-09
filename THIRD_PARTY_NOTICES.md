# Third-party notices and source availability

**This distribution uses agbplay, copyright ipatix and contributors, 2015 onward,
under GNU LGPL version 3.** Its implementation is statically included in the
separate bridge DLL. Both the library and its use are covered by LGPLv3.
The root mGBA license and existing file notices remain unchanged.

## Component licenses

| Component | Local evidence | Supplied material |
| --- | --- | --- |
| mGBA core / modified covered files | Root LICENSE and original MPL headers | MPL-2.0 text; editable modified core source |
| Newly authored MP2K bridge glue | Explicit owner-authorized LGPL-3.0-only notices in bridge.cpp, bridge.h and CMakeLists.txt | Standalone bridge source, ABI/build recipe, license/notice |
| agbplay | Clean external revision 0b87da48d2502da359e45718eec8566ac40fa9d7, root LGPLv3 LICENSE, upstream About copyright | Unmodified library source/headers/notices |
| inih | Vendored LICENSE.txt, BSD-3-Clause | Original notice and source |
| zlib / minizip | Original zlib terms and preserved source notices | Static zlib source/archive; minizip source/notice also retained, not linked in this portable core |
| fmt / Boost headers | MIT / Boost Software License 1.0 | Original notices; exact headers, fmt archive/source |
| libzip / Zstandard | BSD-3-Clause for this build | Notices, source and static archives |
| bzip2 | Original bzip2 license | Notice, source and static archive |
| liblzma | xz COPY files, 0BSD for liblzma | Notices, source and static archive; other upstream tools retain their own terms |
| libgcc / libstdc++ | GPLv3 plus GCC Runtime Library Exception 3.1 | Original texts; unmodified GCC compilation, no proprietary compiler plugin |
| MinGW-w64 / winpthreads | Installed header/runtime license notices | Original file-dependent permissive/public-domain notices |

See [LICENSES/README.md](LICENSES/README.md) for the license map. The package
copies actual original texts, including **GPL-3.0.txt and LGPL-3.0.txt**.
LGPLv3 incorporates GPLv3; supplying only the supplement would be insufficient.
No upstream "or later" grant was established here, so the whole bridge is not
represented as LGPL-3.0-or-later. All independent dependency notices remain.

## Source and relinking — LGPL 4(d)(0)

- `source/mgba-preview-source.zip`: modified MPL mGBA source, original LICENSE
  and notices, core C ABI include copy, release/test/build scripts.
- `source/bridge-source.zip`: canonical bridge glue, C ABI, CMake recipe,
  LGPL/GPL texts and copyright notice.
- `source/agbplay-source.zip`: linked upstream revision's editable source and
  headers, build-related source, original LICENSE and notices; no test mutation.
- `source/relink-support.zip`: exact used support headers, generated zip/zlib
  configuration headers, and six static support archives.
- `source/dependency-source.zip`: preferred support-library source, MSYS2
  recipes/patches and package hash inventory. Unused binary test fixtures and
  CLI translation catalogs are excluded, not code used by the linked libraries.

These materials accompany the binary ZIP without extra charge or a source
request. GPLv3 6(d) is the intended future downloadable-distribution route;
source remains alongside object code, not merely at an unpublished upstream URL.
A physical redistribution must use an applicable GPLv3 section 6 route as well.
A compatible general-purpose compiler/CMake/Ninja and Windows system libraries
are prerequisites, not hidden application materials. No private application
objects are needed: the glue is rebuilt from permitted source. Windows system
DLLs are not redistributed, and no second legacy agbplay DLL is needed.

## Replacement and installation rights

You may modify, rebuild and replace the library/bridge and reverse engineer to
debug those modifications. No distribution term forbids this. The core uses
runtime LoadLibrary/GetProcAddress with no bridge hash/signature gate. Initial
installer hash checks protect supplied package integrity; they do not apply
to runtime loading. Uninstall preserves modified/preexisting files.

Follow [docs/RELINKING.md](docs/RELINKING.md) to rebuild and install a modified
linked version. The documented procedure supplies installation/run information
without passwords, keys or approval. This standalone download does not transfer
a User Product; hardware/device redistribution must reassess GPLv3 6 / LGPL 4(e).

## Audit scope and evidence

[docs/LICENSE_AUDIT.md](docs/LICENSE_AUDIT.md) maps the original MPL 3.1/3.2/3.4,
LGPL 0/2/4 and relevant GPLv3 provisions to package evidence, source provenance,
and the automated modified-library rebuild/load test. The technical audit is
not legal advice or independent copyright-ownership certification. It relies
on the project owner's authorization for newly authored glue. No third-party
work is relicensed. The current static linkage is retained; successful source
recombination avoids a new shared-agbplay ABI/build change before Preview.

Primary terms: [MPL-2.0](https://www.mozilla.org/en-US/MPL/2.0/),
[LGPLv3](https://www.gnu.org/licences/lgpl.html),
[GPLv3](https://www.gnu.org/licenses/gpl.en.html). No ROM, BIOS, game assets,
saves, capture files or private settings are part of the package.

## v0.2-preview scope

The canonical integrated core combines the accepted GBA paths. Original component licenses and the replaceable bridge boundary remain unchanged. The release ZIP supplies editable source and exact relink support alongside object code.

## Metadata attribution and redistribution audit — 2026-10-08

The external text metadata is from No-Intro, distributed in libretro/libretro-database,
revision fbeefcb46c2e1b20a7e2945f34a694a41b2d6f90 (DAT version 2026.08.01).
The pinned repository [license](https://github.com/libretro/libretro-database/blob/fbeefcb46c2e1b20a7e2945f34a694a41b2d6f90/LICENSE)
is Creative Commons Attribution-ShareAlike 4.0 International. Its full text is
supplied as compatibility/LICENSE and LICENSES/CC-BY-SA-4.0.txt.
The derived compatibility data (JSON, CSV, XLSX, their data views and frozen
metadata/evidence inputs) is distributed under CC BY-SA 4.0. Source code retains
its file licenses, including MPL-2.0 and the separately licensed LGPL bridge.

Changes: selected serial-bearing released metadata, excluded labelled non-retail
external entries, normalized fields, joined exact local hashes, deduplicated
identities, and added independently recorded local test classifications. No
support result was inferred from a related region or a matching title.
Credit No-Intro and the libretro database contributors; retain this attribution,
the pinned source link, license link, and modification notice when redistributing.
Share adaptations of the licensed data under the same license. No endorsement is implied.

No-Intro's [Data Usage License](https://datomatic.no-intro.org/terms.html),
checked 2026-10-08, last updated 2026-09-11, also permits lawful publication and
reuse of its data within the rights it controls. The CC attribution/share-alike
requirements of the libretro distribution are preserved here. This audit covers
textual identification/checksum metadata only, not images, games, firmware,
music or video. No-Intro's image policy does not grant third-party redistribution
rights; no images from that site are used. Game names and trademarks belong to
their respective owners; these licenses do not authorize game distribution.
