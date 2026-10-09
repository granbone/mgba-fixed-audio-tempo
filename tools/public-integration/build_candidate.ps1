# SPDX-License-Identifier: MPL-2.0
param(
    [Parameter(Mandatory=$true)][string]$AgbplaySource,
    [string]$ToolBin,
    [string]$RelinkSupport,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$ToolBin) { $ToolBin = Split-Path (Get-Command gcc.exe -ErrorAction Stop).Source }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $sourceRoot 'build-public-integration-preview' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$env:PATH = $ToolBin + ';' + $env:PATH
$coreBuild = Join-Path $outputRoot 'core-build'
$bridgeBuild = Join-Path $outputRoot 'bridge-build'
$argsCore = @('-S',$sourceRoot,'-B',$coreBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    '-DLIBMGBA_ONLY=ON','-DBUILD_LIBRETRO=ON','-DBUILD_STATIC=ON','-DBUILD_SHARED=OFF',
    '-DBUILD_QT=OFF','-DBUILD_SDL=OFF','-DBUILD_TEST=OFF','-DBUILD_SUITE=OFF','-DBUILD_PERF=OFF',
    '-DENABLE_DEBUGGERS=OFF','-DENABLE_SCRIPTING=OFF','-DUSE_FFMPEG=OFF','-DUSE_PNG=OFF',
    '-DUSE_LIBZIP=OFF','-DUSE_LZMA=OFF','-DUSE_JSON_C=OFF','-DUSE_SQLITE3=OFF','-DUSE_ELF=OFF',
    '-DUSE_MINIZIP=ON','-DUSE_EPOXY=OFF','-DUSE_FREETYPE=OFF','-DUSE_DISCORD_RPC=OFF',
    '-DBUILD_GL=OFF','-DBUILD_GLES2=OFF',"-DCMAKE_C_FLAGS=-DENABLE_DIRECTORIES -ffile-prefix-map=$($sourceRoot.Replace('\','/'))=.")
& cmake @argsCore
if ($LASTEXITCODE) { throw 'Core configure failed' }
& cmake --build $coreBuild --target mgba_libretro mgba --parallel 8
if ($LASTEXITCODE) { throw 'Core build failed' }
$argsBridge=@('-S',(Join-Path $sourceRoot 'tools/mp2k-audio-trace/bridge'),'-B',$bridgeBuild,'-G','Ninja',
    '-DCMAKE_BUILD_TYPE=Release',"-DAGBPLAY_SOURCE_DIR=$AgbplaySource",'-DMP2K_PORTABLE_STATIC_DEPS=ON',
    "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$($sourceRoot.Replace('\','/'))=. -ffile-prefix-map=$($AgbplaySource.Replace('\','/'))=external/agbplay")
if ($RelinkSupport) { $argsBridge += "-DMP2K_RELINK_SUPPORT_DIR=$RelinkSupport" }
else { $argsBridge += "-DMP2K_STATIC_LIB_DIR=$(Join-Path $ToolBin '../lib')" }
& cmake @argsBridge
if ($LASTEXITCODE) { throw 'Bridge configure failed' }
& cmake --build $bridgeBuild --parallel 4
if ($LASTEXITCODE) { throw 'Bridge build failed' }
$runtime=Join-Path $outputRoot 'runtime'
New-Item -ItemType Directory -Force $runtime | Out-Null
Copy-Item (Join-Path $coreBuild 'mgba_libretro.dll') (Join-Path $runtime 'mgba_fixed_audio_libretro.dll')
Copy-Item (Join-Path $bridgeBuild 'libmgba_mp2k_bridge.dll') $runtime
$zlib=Join-Path $ToolBin 'zlib1.dll'
if (Test-Path -LiteralPath $zlib) { Copy-Item -LiteralPath $zlib -Destination $runtime }
Get-FileHash (Join-Path $runtime '*.dll') -Algorithm SHA256
