# Metadata Sources / メタデータ出典

Public names are from No-Intro, distributed by libretro/libretro-database. Only text metadata was retrieved. No game binaries were downloaded.

## LIBRETRO_NO_INTRO_GBA

Source: [LIBRETRO_NO_INTRO_GBA](https://raw.githubusercontent.com/libretro/libretro-database/fbeefcb46c2e1b20a7e2945f34a694a41b2d6f90/metadat/no-intro/Nintendo%20-%20Game%20Boy%20Advance.dat)
Acquired/imported: 2026-10-08
License: CC BY-SA 4.0 (libretro distribution)
Scope: Catalogued released identities with explicit four-character serials; labelled prototypes, demos, unlicensed, BIOS and test programs excluded. Entries without a serial require separate release verification and are omitted. Not a completeness or licensing guarantee.
Pinned revision: fbeefcb46c2e1b20a7e2945f34a694a41b2d6f90
DAT SHA256: 90188f6e4e481cb2be98eda6271844b7f56813575497bebaaeddf7b76a6353db

## EXACT_IDENTITY_EVIDENCE

Source: [EXACT_IDENTITY_EVIDENCE](sources/runtime-eligibility.json)
Acquired/imported: 2026-10-09
License: CC BY-SA 4.0
Scope: Hash-only analysis and production static eligibility. No audio correctness inferred.

## LIMITED_RUNTIME_TESTS

Source: [LIMITED_RUNTIME_TESTS](sources/v03-runtime-observations.json)
Acquired/imported: 2026-10-09
License: CC BY-SA 4.0
Scope: Scenario-specific observations. Prior limited PCM/event acceptance retained separately.

## Attribution and redistribution audit — 2026-10-09

The pinned libretro database [LICENSE](https://github.com/libretro/libretro-database/blob/fbeefcb46c2e1b20a7e2945f34a694a41b2d6f90/LICENSE) is Creative Commons Attribution-ShareAlike 4.0 International. Original texts are retained in compatibility/LICENSE and LICENSES/CC-BY-SA-4.0.txt. Credit No-Intro and libretro database contributors, retain the source and license links and modification notice, and share adaptations under CC BY-SA 4.0. No endorsement is implied.

Modifications: selected serial-bearing released metadata, excluded labelled non-retail external entries, normalized fields, joined exact hash evidence, deduplicated identities, and added scenario-specific driver/runtime classifications. Unmatched records are excluded from the public view. No related region inherits a test result.

The [No-Intro Data Usage License](https://datomatic.no-intro.org/terms.html), checked 2026-10-09 (last updated 2026-09-11), permits lawful reuse within rights controlled by its operator. The libretro attribution/share-alike terms are preserved. Game names and trademarks belong to their owners. Metadata licenses do not authorize game, firmware, image, music or video redistribution. No such material is included.

The DAT scope is explicit in the source snapshot. Entries without a serial and labelled prototype/demo/beta/unlicensed/BIOS/test records were excluded; this can omit legitimate releases and is not a completeness claim. Regional binaries sharing one SHA1/size count once.

Join order: full SHA1 with size and CRC32 consistency; uniquely matching CRC32+size only when a full hash is unavailable; game code is a supplemental consistency check. A contradictory full hash never falls back to a weaker match. Title similarity is never a join key. DAT revision text is preserved; ROM header revision is separately recorded.

Static eligibility is measured by the production bridge scanner and core profile builder. It confirms a trial path, not live structure validity, bridge success or audio accuracy. The public database contains no ownership statistics or filenames. Original internal evidence and unmatched reports remain private.

The compatibility data and evidence snapshots are CC BY-SA 4.0. Source code remains MPL-2.0 or its existing file license; separate bridge/agbplay code remains LGPLv3. The v0.3 RC is prepared locally and is not published. Its package includes matching editable sources, dependency notices and relink support as described in THIRD_PARTY_NOTICES.md.
