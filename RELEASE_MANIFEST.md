# v0.3-preview distribution provenance

This formal prerelease uses the tested v0.3 RC runtime, with SHA256 calculated from the actual packaged files. It does not reuse the v0.2 core or bridge. The recording and distribution core bytes are identical.

Production code originates from development checkpoint `75872eb6899ce266e2cae452b5e2a1154d185a46`; the clean export was selected from `e1d95341aba88cdf0d87f03cf42aa1f6fed038e5`. These identifiers record provenance only. Neither old Git history nor old tags are present in this repository.

Normalized production-source content SHA256 (src/, include/, CMakeLists.txt, version.cmake): `1DE978D3E02624657D1287370130451C2F878364DCEF65EB83EA24A3DC454F5B`.

The package manifest records the actual new public-source commit/tree and freshly calculated DLL/file hashes. Complete source, pinned agbplay `0b87da48d2502da359e45718eec8566ac40fa9d7`, bridge glue, original licenses, headers/static relink archives and dependency preferred sources are supplied. Relink replacement is allowed without a signature/hash gate. See docs/RELINKING.md and docs/LICENSE_AUDIT.md.

The RC's embedded Git-derived version string references its former ancestry. A fresh source rebuild can report different version metadata; this is documented rather than presented as binary-identical reproduction.

The existing v0.2 distribution and all former history remain in the separately identified Private archive. Old demo images/logs/footage are excluded from this clean public history. Two unchanged v0.3 demonstration MP4s are separate Release assets, adopted and rights-confirmed by the user; no independent third-party permission is claimed. No ROM/BIOS/save/state/raw PCM is distributed.

Compatibility counts: 3,075 public DAT identities; static trials 601 = 32 limited passes + 567 unverified + 2 observed fallback. Total fallback 6, unsupported 9, unknown driver 183, not analyzed 2,278. 3x and GB/GBC research are not production promises. Long-rate ownership-recovery fallback remains a documented inherited limitation.
