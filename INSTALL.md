# Install mGBA Fixed Audio Tempo v0.3-preview

Download the Windows x64 ZIP from the v0.3-preview prerelease linked in README. Extract it to a new directory. Keep this experimental core separate from official mGBA and older installations. Complete corresponding source and LGPL relink materials are included; see [BUILDING.md](BUILDING.md).

Use the packaged install_to_retroarch.ps1 with your RetroArch directory, or copy the core and matching bridge to a separate cores directory following the Windows layout below. The installer preserves conflicting/preexisting files; do not manually overwrite a user's older core or modified bridge.

For isolated manual use, copy the new core and matching libmgba_mp2k_bridge.dll to a separate core directory. Select Fixed Audio Tempo Mode in RetroArch. The default is Experimental; choose Conservative or Disabled as needed, save the core option, then reload content.

Back up saves and states before testing. See [Windows layout and controls](docs/INSTALL_WINDOWS.md) and [experimental warnings](README.md).
