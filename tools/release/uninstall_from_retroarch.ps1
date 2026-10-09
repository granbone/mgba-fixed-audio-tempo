# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][Alias('RetroArchDir')][ValidateNotNullOrEmpty()][string]$RetroArchPath, [switch]$RestoreUserEnvironment)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
function Get-PreviewHash([string]$Path) {
 $algorithm=[Security.Cryptography.SHA256]::Create()
 try { return ([BitConverter]::ToString($algorithm.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-','') }
 finally { $algorithm.Dispose() }
}
$root = (Resolve-Path -LiteralPath $RetroArchPath).Path
$recordPath = Join-Path $root '.fixed-audio-preview-install.json'
if (-not (Test-Path -LiteralPath $recordPath)) { throw 'No Fixed Audio Preview install record found. No files removed.' }
$record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
if ($record.schema -ne 1 -or $record.product -ne 'mGBA Fixed Audio Tempo Preview') { throw 'Unknown install record; no files removed.' }
# The previous bridge subdirectory remains allowlisted for old install records.
# Removal still requires an owned file with the recorded hash.
$allowed = @('cores/mgba_fixed_audio_libretro.dll','cores/libmgba_mp2k_bridge.dll','info/mgba_fixed_audio_libretro.info','libmgba_mp2k_bridge.dll','fixed-audio-preview-runtime/libmgba_mp2k_bridge.dll')
foreach ($item in $record.files) {
 if ($item.path -notin $allowed -or $item.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid install record path/hash; no files removed.' }
}
foreach ($item in $record.environment) {
 if ($item.name -notin @('MGBA_FIXED_AUDIO_TEMPO','MGBA_MP2K_BRIDGE_PATH')) { throw 'Unknown environment record; no files removed.' }
}
$retained = @()
foreach ($item in $record.files) {
 $destination = Join-Path $root $item.path
 if ($item.owned -and (Test-Path -LiteralPath $destination)) {
  if ((Get-PreviewHash $destination) -eq $item.sha256) { Remove-Item -LiteralPath $destination; Write-Output "Removed $($item.path)" }
  else { $retained += $item; Write-Output "Preserved modified file: $($item.path)" }
 }
}
$pendingEnvironment = @()
foreach ($item in $record.environment) {
 if ($RestoreUserEnvironment) {
  $current = [Environment]::GetEnvironmentVariable($item.name, 'User')
  if ($current -eq $item.installed) { [Environment]::SetEnvironmentVariable($item.name, $item.previous, 'User'); Write-Output "Restored $($item.name)" }
  else { Write-Output "Preserved environment changed by user: $($item.name)" }
 } else { $pendingEnvironment += $item }
}
if ($retained.Count -or $pendingEnvironment.Count) {
 $record.files = @($retained)
 $record.environment = @($pendingEnvironment)
 $record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $recordPath -Encoding UTF8
 Write-Output 'Install record retained for preserved files or environment restoration.'
} else { Remove-Item -LiteralPath $recordPath; Write-Output 'Preview uninstalled. Official cores, saves, and config were not included in removal.' }
