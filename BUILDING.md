# Build and replace the preview candidate

The tested target is Windows x64 libretro. The actual toolchain was MinGW-w64 GCC/G++ **16.2.0**, CMake **4.4.3**, Ninja **1.13.2**, and Python **3.12** for automation. The bridge uses C++23 and requires CMake 3.20 or newer. This build disables Qt, SDL, scripting, debuggers, FFmpeg, PNG, JSON, SQLite, ELF, and other optional frontend dependencies.

The core source is this repository. The separate bridge uses an external clean agbplay checkout at **0b87da48d2502da359e45718eec8566ac40fa9d7**. No new submodule is introduced. The candidate includes editable mGBA/bridge/agbplay source and matching dependency/relink materials alongside the binaries.

Installed bridge dependencies used by the build: fmt 12.2.0, Boost headers 1.92.0, libzip 1.11.4, bzip2 1.0.8, liblzma/xz 5.8.4, Zstandard 1.5.7, zlib 1.3.2, plus pkg-config. The six linked static support archives match the supplied relink-support archive by SHA256. The portable bridge imports Windows system DLLs only. The configured core has USE_ZLIB and USE_MINIZIP disabled after dependency detection; `zlib1.dll` is nevertheless supplied as the established preview dependency, and the bridge contains static zlib. Inspect actual imports rather than assuming extra runtime DLLs.

## Installed-toolchain build

From the source root, with MinGW tools on PATH:

```powershell
./tools/public-integration/build_candidate.ps1 `
  -ToolBin '<MinGW x64 bin directory>' `
  -AgbplaySource '<clean agbplay source directory>'
```

The script shows the exact CMake switches and produces `build-public-integration-preview/runtime/mgba_fixed_audio_libretro.dll`, `libmgba_mp2k_bridge.dll`, and, when installed, `zlib1.dll`. Build directories are ignored. It does not install files, change ROMs, or publish anything. Configure and build failures stop the script.

## Rebuild the LGPL bridge from supplied materials

Extract `source/bridge-source.zip`, `source/agbplay-source.zip`, and `source/relink-support.zip` into three separate new directories. The bridge ZIP includes its original LGPL/GPL license texts and CMake recipe. The support ZIP contains the exact headers and six static archives used here; `source/dependency-source.zip` contains editable dependency sources and package recipes/patches.

```powershell
cmake -S '<bridge source>' -B '<new bridge build>' -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  '-DAGBPLAY_SOURCE_DIR=<agbplay source>' `
  '-DMP2K_RELINK_SUPPORT_DIR=<support directory>' `
  -DMP2K_PORTABLE_STATIC_DEPS=ON
cmake --build '<new bridge build>' --parallel 4
```

Modify and rebuild the library or glue as desired, retain the C ABI, close RetroArch, back up the existing bridge, and replace `libmgba_mp2k_bridge.dll` beside the candidate core. The runtime loader has no library hash/signature gate. You may replace the library and reverse engineer for debugging those modifications. No private application objects or signing keys are needed.

Source ZIPs have no git history, so their upstream version fallback string can differ from a checkout build. The manifest separately identifies the production source checkpoint, integrated documentation commit and hashes of the actually tested DLLs. These are source/build reproducibility instructions, not a promise of identical PE timestamps or compiler-version-independent binary hashes.

Regression tools require locally supplied exact ROM identities and states. They are not bundled. `validation-summary.json` records actual canonical-build tests, including expected native fallbacks; compatibility statuses are not inferred from scanning alone.
