# Rebuild and replace the LGPL bridge

This package supplies source and relinking materials for **LGPLv3 section
4(d)(0)**. The bridge contains agbplay statically; replacing an unrelated legacy
agbplay DLL does not change this bridge. Rebuild the entire bridge with your
modified agbplay and replace `libmgba_mp2k_bridge.dll` instead.

## 1. Prerequisites

Use Windows x64, a compatible MinGW-w64 GCC toolchain (the Preview used GCC
16.2.0), CMake 3.20 or newer, Ninja, and PowerShell. Python is needed only for
automated tests. These general-purpose tools and Windows system libraries are
not bundled. No existing development checkout, private configuration, installed
fmt/Boost/libzip package, signing key or repository access is needed for this
bridge rebuild. Use an x64 target compatible with the Preview core.

## 2. Extract the source materials

Extract the Preview ZIP. From that package folder, choose a **new** work folder:

```powershell
$work = Join-Path $PWD 'relink-work'
New-Item -ItemType Directory -Path $work
Expand-Archive .\source\bridge-source.zip (Join-Path $work 'bridge')
Expand-Archive .\source\agbplay-source.zip (Join-Path $work 'agbplay')
Expand-Archive .\source\relink-support.zip (Join-Path $work 'support')
```

`source/mgba-preview-source.zip` contains modified MPL mGBA source and an
identical copy of the bridge source for the core's C ABI include.
`source/dependency-source.zip` contains editable support-library source and
MSYS2 recipes/patches. `support` contains the exact linked static archives and
required headers, so you can use those without rebuilding the support libraries.
The canonical standalone bridge is the `bridge` folder above.

## 3. Modify agbplay

Edit files under `relink-work/agbplay/src/agbplay`. Preserve upstream notices.
Keep the bridge's C ABI compatible with the Preview core. Mark modified source
and its modification date if redistributing it (GPLv3 section 5, as incorporated
by LGPLv3). The shipped library source is unmodified; the audit's test identifier
was added only to an external test copy.

## 4. Build

Set `$toolBin` to your MinGW toolchain's `bin` folder:

```powershell
$toolBin = '<your MinGW bin folder>'
$work = (Resolve-Path .\relink-work).Path
$env:PATH = $toolBin + ';' + $env:PATH
& "$toolBin\cmake.exe" -S "$work\bridge" -B "$work\build" -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_CXX_COMPILER=$toolBin/g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$toolBin/ninja.exe" `
  "-DAGBPLAY_SOURCE_DIR=$work/agbplay" `
  "-DMP2K_RELINK_SUPPORT_DIR=$work/support" `
  -DMP2K_PORTABLE_STATIC_DEPS=ON
if ($LASTEXITCODE) { throw 'Configure failed' }
& "$toolBin\cmake.exe" --build "$work\build" --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
```

The result is `relink-work/build/libmgba_mp2k_bridge.dll`. Both glue and all
20 agbplay translation units are rebuilt from source. Generated Ninja/CMake
files are regenerated automatically. No proprietary application object files
are needed. Inspect imports with `objdump -p` if desired; the portable bridge
uses Windows system DLLs only.

## 5. Replace the installed bridge

Close RetroArch first. Set `$retroArchDir` to the folder where the Preview was
installed. Back up the original bridge before replacing it:

```powershell
$retroArchDir = '<RetroArch>' # Replace with your folder containing retroarch.exe.
$installed = Join-Path $retroArchDir 'cores/libmgba_mp2k_bridge.dll'
$backup = $installed + '.original'
if (Test-Path -LiteralPath $backup) { throw 'Choose a new backup name' }
Copy-Item -LiteralPath $installed -Destination $backup
Copy-Item -LiteralPath "$work\build\libmgba_mp2k_bridge.dll" -Destination $installed -Force
```

The core uses runtime `LoadLibrary`/`GetProcAddress`. It imposes no signature,
bridge hash, registration or approval requirement. Installer integrity checks
apply to initial package installation; they do not gate runtime loading.
Uninstall deliberately preserves files whose hash has changed. Restore your
backup before uninstalling if you want the original installation record to
remove the bridge; otherwise manage your modified DLL yourself.

## 6. Run and confirm

From the extracted package, run:

```powershell
.\launch_fixed_audio.ps1 -RetroArchPath $retroArchDir -Rom '<your own GBA ROM>'
```

The launcher uses the installed replacement bridge. Enable RetroArch logging
and confirm `FIXED AUDIO ACTIVE` with `bridge=loaded`. This establishes active
Fixed Audio; add your own harmless library identifier or use a debugger/module
viewer to establish which modified build was loaded. The automated audit test
adds a constructor identifier to agbplay, records it from the running process,
checks the installed DLL hash, and verifies active output with the unchanged
Preview core. It does not modify the shipped library source.

To restore the original, close RetroArch and copy the backup over the modified
DLL. After changing User environment variables, restart the parent launcher or
frontend before relaunching RetroArch (Steam is one optional example). Ordinary
file replacement at the same path requires only restarting RetroArch.

## Redistribution

Preserve the LGPLv3/GPLv3 texts, library copyright notice, modification notices,
source/relink materials, and dependency notices. Do not restrict modification
or reverse engineering for debugging modifications. This free-standing Windows
download transfers no User Product; the GPLv3 section 6 device-transaction
trigger is therefore not assumed. The above installation/run procedure is
supplied anyway; a future device bundle must reassess section 4(e).
