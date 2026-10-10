# v0.4-preview source, build and relinking

The final distribution retains the exact RC1 core, bridge and dependency DLLs.
Matching binary source commit: `d451f54da8aea4217130574df42aee775cd369a5`.
The manifest separately records the clean public source/distribution commit.
The final public source's compiled files are compared byte for byte with the
matching binary-source archive before packaging. Git version strings can differ
on a new rebuild; a distribution commit is not a new audio implementation.

`source/mgba-preview-source.zip` contains matching complete product/build source.
`source/mgba-rc-distribution-source.zip` contains the final clean public source
and documents (the filename is retained for existing tooling compatibility).
Private non-build Phase reports and path-specific diagnostic helpers are omitted;
their original local history and evidence remain protected. All compiled files,
headers and build/relink recipes are included. Third-party source/support archives
are supplied separately to avoid duplicate embedding.

Pinned agbplay source: `0b87da48d2502da359e45718eec8566ac40fa9d7`.
The supplied bridge CMake recipe generates `SequenceReader-stable.cpp` using
the deterministic PSG tie ordering and saturating player/track priority patch.
`priority_order.hpp` and the complete patch recipe are included. The original
editable agbplay source and LGPL/GPL license texts remain intact.

Use [BUILDING](../BUILDING.md) and [RELINKING](RELINKING.md) with separate new
source/build directories. The recorded toolchain is MinGW GCC/G++ 16.2.0,
CMake 4.4.3, Ninja 1.13.2, Python 3.12. Editable sources for the six dependency
components and the required static support libraries/headers are included.
The modified-library relink test changes only a disposable source copy and loads
it using the unchanged core. Original package files are never changed by tests.

The diagnostic frontend adapter is not a distributed runtime component. It
forwards to the unchanged core and requests real RetroArch fast-forward rates.
Measurements use the GBA core frameCounter, not display FPS or nominal settings.

The package's SHA256SUMS covers every file except itself. The separate Release
asset SHA256SUMS covers the final ZIP and the two unchanged adopted MP4s.
The external integrity manifest also hashes both checksum files. Media are
Release assets only: no videos, WAVs, PCM, ROMs, BIOS, saves or states in Git/ZIP.
This procedure prepares local assets only; it performs no push, tag or upload.
