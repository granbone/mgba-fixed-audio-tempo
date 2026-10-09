# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][Alias('RetroArchDir')][ValidateNotNullOrEmpty()][string]$RetroArchPath, [Parameter(Mandatory=$true)][string]$Rom, [string]$BridgePath, [int]$MaxFrames=0, [switch]$Hidden)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RetroArchPath).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$exe = Join-Path $root 'retroarch.exe'
$core = Join-Path $root 'cores/mgba_fixed_audio_libretro.dll'
$bridge = if ($BridgePath) { (Resolve-Path -LiteralPath $BridgePath).Path } elseif (Test-Path -LiteralPath (Join-Path $root 'cores/libmgba_mp2k_bridge.dll')) { Join-Path $root 'cores/libmgba_mp2k_bridge.dll' } else { Join-Path $root 'libmgba_mp2k_bridge.dll' }
foreach ($file in @($exe,$core,$bridge)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing installed file: $file" } }
$config = [IO.Path]::GetTempFileName()
$names = @('MGBA_MP2K_BRIDGE_PATH')
$previous = @{}
foreach ($name in $names) { $previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
try {
 @('fastforward_ratio = "2.0"','input_toggle_fast_forward = "space"','config_save_on_exit = "false"') | Set-Content -LiteralPath $config -Encoding ASCII
 # The saved Core Option controls the mode. Preserve any existing explicit OFF.
 $env:MGBA_MP2K_BRIDGE_PATH=$bridge
 $arguments = @('--appendconfig',$config,'-L',$core,$romPath)
 if ($MaxFrames -gt 0) { $arguments = @("--max-frames=$MaxFrames") + $arguments }
 if ($Hidden) {
  $quoted=@($arguments | ForEach-Object { '"'+$_+'"' })
  $process=Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
  if ($process.ExitCode -ne 0) { throw "RetroArch exited with code $($process.ExitCode)" }
 } else {
  # Native argument arrays preserve spaces in user-selected paths.
  Push-Location -LiteralPath $root
  try { & $exe @arguments; if ($LASTEXITCODE -ne 0) { throw "RetroArch exited with code $LASTEXITCODE" } }
  finally { Pop-Location }
 }
} finally {
 foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
 Remove-Item -LiteralPath $config -ErrorAction SilentlyContinue
}
