# v0.4-preview RC build and relinking

This local RC retains the exact Phase9 final runtime. Phase10 changes packaging,
compatibility evidence and private test tooling; it does not alter core/bridge DSP,
driver profiles, STOP semantics, ownership rules or safety guards.

The DLL source commit is `d451f54da8aea4217130574df42aee775cd369a5`.
`source/mgba-preview-source.zip` is the complete matching source from that commit.
`source/mgba-rc-distribution-source.zip` contains the clean committed RC source,
including the current database, documentation and diagnostic tools. The manifest
records both commits. The package builder rejects compiled-source differences.
Git-derived build version metadata refers to the binary build commit; the RC
package version describes the distribution and does not imply a new DSP build.
Non-build historical private Phase reports and retention instructions are omitted
from both source archives. All corresponding code, headers, build/relink recipes,
licenses and dependency material remain intact; compiled source bytes are unchanged.
Eight historical local diagnostic helpers containing private paths are also omitted
from the ZIP export; they remain in the protected local repository. Third-party
archives are supplied separately, avoiding redundant embedding inside both core
source archives. The corresponding product/build source is complete.

The agbplay archive is the original pinned source. The supplied bridge CMake recipe
creates a patched `SequenceReader-stable.cpp` in the build directory and includes
the supplied `priority_order.hpp`: deterministic PSG tie selection and saturating
player+track priority remain in place. Original agbplay source is unchanged.
The full recipe, headers, LGPL/GPL texts, six support archives and editable dependency
sources are included. No private test mutations are distributed.

Extract matching core, agbplay and support sources into separate new directories.
Use the matching source's `tools/public-integration/build_candidate.ps1` with
`-ToolBin`, `-AgbplaySource`, `-RelinkSupport` and a new `-OutputDirectory`.
See [BUILDING](../BUILDING.md) and [LGPL relinking](RELINKING.md) for exact commands.
The recorded toolchain is MinGW GCC/G++16.2.0, CMake4.4.3, Ninja1.13.2, Python3.12.

The private frontend adapter is diagnostic tooling only and is excluded from
runtime files. It loads the unchanged RC core and requests real RetroArch
`RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE`; it never spoofs throttle reports,
GBA frames, audio clocks or PCM. Internal frameCounter deltas measure actual speed.

`SHA256SUMS.txt` covers package files except itself. The external integrity record
also hashes the checksum file and the complete ZIP, avoiding a self-hash cycle.
Install/relink tests use separate sandbox directories. Do not overwrite published
v0.3 DLLs. No push, tag, Release or video upload is part of this procedure.
