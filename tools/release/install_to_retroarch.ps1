# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][Alias('RetroArchDir')][ValidateNotNullOrEmpty()][string]$RetroArchPath, [switch]$SetUserEnvironment)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
function Get-PreviewHash([string]$Path) {
 $algorithm=[Security.Cryptography.SHA256]::Create()
 try { return ([BitConverter]::ToString($algorithm.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-','') }
 finally { $algorithm.Dispose() }
}
$root = (Resolve-Path -LiteralPath $RetroArchPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $root 'retroarch.exe') -PathType Leaf)) { throw 'Select the RetroArch folder containing retroarch.exe.' }
$recordPath = Join-Path $root '.fixed-audio-preview-install.json'
if (Test-Path -LiteralPath $recordPath) { throw 'An installation record already exists. Uninstall this Preview before installing again.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package-manifest.json') -Raw | ConvertFrom-Json
$allowed = @{
 'cores/mgba_fixed_audio_libretro.dll' = 'cores/mgba_fixed_audio_libretro.dll'
 'info/mgba_fixed_audio_libretro.info' = 'info/mgba_fixed_audio_libretro.info'
 'runtime/libmgba_mp2k_bridge.dll' = 'cores/libmgba_mp2k_bridge.dll'
}
$plan = @()
foreach ($sourceName in $allowed.Keys) {
 $entry = @($manifest.files | Where-Object { $_.path -eq $sourceName })
 if ($entry.Count -ne 1) { throw "Missing package manifest entry: $sourceName" }
 $source = Join-Path $PSScriptRoot $sourceName
 if ((Get-PreviewHash $source) -ne $entry[0].sha256) { throw "Package hash mismatch: $sourceName" }
 $relative = $allowed[$sourceName]
 $destination = Join-Path $root $relative
 $exists = Test-Path -LiteralPath $destination
 if ($exists -and (Get-PreviewHash $destination) -ne $entry[0].sha256) { throw "Existing different file preserved: $destination" }
 $plan += [pscustomobject]@{ path=$relative; sha256=$entry[0].sha256; owned=(-not $exists); source=$source }
}
$environment = @()
$copied = @()
try {
 foreach ($item in $plan) {
  if ($item.owned) {
   $destination = Join-Path $root $item.path
   [void](New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force)
   Copy-Item -LiteralPath $item.source -Destination $destination
   $copied += $item
  }
 }
 if ($SetUserEnvironment) {
  $values = @{ MGBA_MP2K_BRIDGE_PATH=(Join-Path $root 'cores/libmgba_mp2k_bridge.dll') }
  foreach ($name in $values.Keys) {
   $old = [Environment]::GetEnvironmentVariable($name, 'User')
   $environment += [pscustomobject]@{ name=$name; previous=$old; installed=$values[$name] }
   [Environment]::SetEnvironmentVariable($name, $values[$name], 'User')
  }
 }
 [pscustomobject]@{ schema=1; product='mGBA Fixed Audio Tempo Preview'; files=@($plan | Select-Object path,sha256,owned); environment=@($environment) } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $recordPath -Encoding UTF8
} catch {
 foreach ($item in $copied) {
  $destination = Join-Path $root $item.path
  if ((Test-Path -LiteralPath $destination) -and (Get-PreviewHash $destination) -eq $item.sha256) { Remove-Item -LiteralPath $destination }
 }
 foreach ($item in $environment) { [Environment]::SetEnvironmentVariable($item.name, $item.previous, 'User') }
 throw
}
Write-Output "Installed custom Preview core in $root"
if ($SetUserEnvironment) { Write-Output 'User environment configured. Close RetroArch and fully restart its parent launcher/frontend or terminal before relaunching. If values remain stale, sign out/in.' }
