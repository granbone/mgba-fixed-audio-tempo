# mGBA Fixed Audio Tempo v0.4-preview

Prerelease candidate; prepared locally and awaiting final publication instruction.
Planned tag: `v0.4-preview`. No v0.4 Release link exists yet.

- Keep supported GBA BGM at normal tempo and pitch during actual 2x/3x gameplay,
  using an independent audio clock and event-driven synthesis.
- Preserve Experimental, Conservative and Disabled modes, explicit Disabled
  settings, ownership validation and safe native fallback.
- Improve MP2K player priority and deterministic PSG tie ordering; retain the
  existing limited SFX lifetime fixes and B6JJ's separate backend.
- Add scoped BGM evidence for 20 exact public DAT identities. The database has
  3,075 identities and 601 Experimental trial candidates. These results are not
  full playthroughs, all-scene guarantees or sound-effect certification.
- Adopt the user-reviewed FFTA active battle and Oriental Blue continuous walking
  demos, each switching 1x→2x→3x twice. FFTA explicitly marks a state reload/audio
  cut near 19.15 seconds. The viewed footage has no reported major visual/audio issue.

Known limits: some finite SE may end early at 2x/3x; continuing SE are discarded
on State Load/Rewind; FFTA reconstruction can take longer; runtime checks can
retain native audio. Unverified titles may have audio errors, freezes or crashes.
Unknown/Unlimited speeds safely use native audio, which does not preserve fixed
tempo. GB/GBC Fixed Audio remains research. Six Phase9 titles are still unconfirmed:
B3DJ, BFTJ, BIXJ, AB2J, A5BJ and BKRJ.

The Windows x64 ZIP includes core, MP2K bridge, dependency DLL, Compatibility DB,
complete corresponding source, build/relink materials, original licenses and
per-file SHA256. The two unchanged MP4s and asset SHA256SUMS are separate assets.
No ROM, BIOS, save, state or private audio is included. Public v0.3 is preserved.
See [final audit](V04_FINAL_RELEASE_AUDIT.md) for exact tests and remaining review.
