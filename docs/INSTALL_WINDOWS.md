# Windows x64 prerelease layout and controls

v0.3-preview is the current experimental prerelease. The old v0.2-preview distribution is preserved in a separate Private archive.

The candidate bridge can live beside mgba_fixed_audio_libretro.dll. The new installer recipe uses cores/libmgba_mp2k_bridge.dll so normal RetroArch core loading works without environment setup. An explicit MGBA_MP2K_BRIDGE_PATH can select a rebuilt replacement. The launcher supports the previous bridge-at-frontend-root layout and preserves existing OFF controls.

The saved Fixed Audio Tempo Mode controls Experimental / Conservative / Disabled. The launcher sets fixed 2x and the bridge path for its child; it no longer forces TEMPO=1. Changing the mode requires closing/reloading content. Reset alone retains the existing mode.

Before using any candidate, back up saves and savestates. Unverified games may crash, freeze, produce incorrect audio, or experience save-data loss. Use Conservative or Disabled if problems occur.

The installer preserves official mGBA, conflicting/preexisting files and modified bridges. Uninstall removes only owned files whose recorded hashes still match; old installation records remain supported. Candidate packaging must regenerate its own manifest and supply the matching source/licenses/relink materials. No v0.2 manifest or DLL is overwritten.

[Build](../BUILDING.md) · [Sources and relinking](RELINKING.md)
