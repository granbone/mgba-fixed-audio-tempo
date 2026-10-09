# MP2K bridge source notice

Original bridge glue (`bridge.cpp`, `bridge.h`, `CMakeLists.txt`):
Copyright (c) 2026 granbone. Licensed under **LGPL-3.0-only**.
Explicit file notices were added on 2026-10-03, under the project owner's
authorization for this newly authored code. No third-party code is relicensed.
The root mGBA MPL license does not replace this directory's license.

The external agbplay implementation is copyright ipatix and contributors,
2015 onward, and is licensed under GNU LGPL version 3 at revision
`0b87da48d2502da359e45718eec8566ac40fa9d7`. The upstream grant examined here
does not establish an additional "or later" permission. Its source/notices
are preserved separately. This directory does not contain copied agbplay
implementation files; CMake compiles the supplied external source.

The distributed bridge combines this glue and agbplay under LGPLv3,
with the original terms/notices of permissive dependencies and the GCC
Runtime Library Exception retained. `LICENSE` is the LGPLv3 supplement;
`COPYING.GPL-3.0.txt` is the GPLv3 text it incorporates. Both are required.

Modification, rebuilding, replacement, redistribution under these terms,
and reverse engineering for debugging modifications are permitted.
There is no warranty. See the license texts and `docs/RELINKING.md` in the
Preview package for source locations and replacement instructions.
