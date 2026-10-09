# v0.3 RC validation

Production checkpoint: `75872eb6899ce266e2cae452b5e2a1154d185a46`. Fresh Windows x64 core SHA256: `A4BD66BA6A50CFC59109971DC1937BD3675991EE5E302FD3C1554F93C114BBF1`; bridge: `6C3EFC3F5FEDA53052A4432322A62357925842B79C54396479FFFCE857D29A40`. This is not the v0.2 canonical DLL. Source changes after that checkpoint are documentation/data/testing/packaging changes, with production code retained.

## Core and audio observations

- Fresh default, Experimental, Conservative and Disabled settings: 8/8 actual RetroArch launch/restart cases passed. Explicit Disabled remains disabled. Content reload is required for a mode change.
- Representative regression: 12/12 jobs passed on this RC, including AAMJ golden PCM/events, eleven FFTA golden comparisons, late-state recovery, AFEJ modulation/rewind and hard guards, a fourteen-identity mode/speed suite, and B6JJ golden/load/rewind/OFF cases.
- Additional audio probes: fourteen release identities, including an independently matched AE2J revision; 56 runs covering normal, 2x, switches and load/rewind recovery. Twelve identities observed ACTIVE in these scenarios; U3IJ and AG7J remained native. No crash or hang was observed. Detailed sanitized results are in [V03_RC_AUDIO.json](V03_RC_AUDIO.json).
- BGM/SE listening remains HUMAN_REVIEW_REQUIRED for these automated boot/menu probes. Event counts and ACTIVE do not certify audible correctness. No newly unverified identity was promoted to LIMITED_TEST_PASS merely for activation or a waveform segment.
- New actual Windows/OBS FFTA and AORJ recording checks are separate selected-scene evidence; see [V03_DEMO_REVIEW.md](V03_DEMO_REVIEW.md). FFTA's preceding battle clip received a user report of no BGM/SE/synchronization discomfort. The user has now adopted both finished files and confirms game-footage/audio rights for publication; this does not independently certify third-party permission. Verified Release assets are linked from README.

The initial 601 trial-eligible count differed from 32 + 568 because AG7J was statically eligible but had observed native fallback. New RC testing also observed U3IJ fallback; the updated split is **601 = 32 limited passes + 567 unverified trials + 2 statically eligible native-fallback identities**. All 608 matched MP2K probe identities were re-probed against the new production bridge without an eligibility change; 600 were statically eligible and eight had ambiguous tables, plus the one custom-backend identity makes 601.

## Performance and stability

Serialized deterministic-host measurements: 54 short runs, three repetitions per game/mode/speed condition, plus three 108,000-frame A2QJ stress runs. Process CPU time per emulated frame, pooled normal/2x median:

| Game code | Experimental, ms/frame | Conservative, ms/frame | Disabled, ms/frame |
| --- | ---: | ---: | ---: |
| AAMJ | 0.3021 | 0.2917 | 0.2708 |
| A2QJ | 0.4141 | 0.1172 | 0.1198 |
| A8CJ | 0.1589 | 0.1042 | 0.0677 |

Experimental adds workload, especially when the other modes exclude the unverified backend. These are headless throughput measurements including disk/capture overhead, not a hardware-independent performance promise. A2QJ's Experimental stress completed in 252.9134 wall seconds, equivalent to 30.1 emulated minutes and 7.15x unthrottled capacity; Conservative/Disabled used native audio and completed in 53.7179/53.4330 seconds. This is **not** a 30-minute real-time soak. Observed core ring under/overrun counters were zero, with no Experimental safety fallback in that run. Physical-device underruns were not instrumented.

Six muted actual RetroArch D3D11/XAudio2 runs also completed. Experimental CPU was 0.2188 seconds for the normal 900-frame run and 0.2031 seconds for the 2x run; Conservative/Disabled 2x CPU was 0.1719 seconds. Boot-inclusive 2x wall times were 9.2535/9.1769/9.1327 seconds. Startup and the initial 2-second 1x interval are included, so their whole-run average speed is not a direct steady-state 2x measurement. The separate unmuted OBS recordings measured steady 2x progression near 2.00x. Sanitized benchmark results: [V03_RC_PERFORMANCE.json](V03_RC_PERFORMANCE.json).

## Long-rate recovery limitation

Eight direct comparisons against the unchanged v0.2 canonical core completed. One-frame timing disturbance recovered on both cores; 40/80-frame disturbances produced the same sticky `STATE_LOAD_UNVERIFIED_OWNERSHIP` native fallback on both. Disabled used native audio. Live SoundInfo rate index was 12 while the profile expectation was 3; observed sample count 704 and DMA period 2 derive a different current FIFO span than the fixed expectation. The original historical harness's unconditional rebind expectation therefore remains unsatisfied.

This is an inherited safe recovery limitation with an overstrong historical expectation; it has not been conclusively reduced to a test-only defect or fixed. No v0.3-only regression was observed in the comparison. The ownership guard remains strict, and no safety guard was relaxed to make the fixture pass.

## Compatibility and compliance

JSON/CSV/XLSX/Markdown and README totals agree: 3,075 DAT releases, 32 limited pass, 567 experimental unverified, 6 native fallback, 9 unsupported, 183 unknown driver, 2,278 not analyzed, 601 statically trial eligible. Titles/regions/revisions derive from exact DAT identities. All eight workbook sheets round-trip, retain filters/frozen panes and share the same summary. Public views contain no ownership statistics, private filenames or obsolete v1 public-status categories. Regional variants do not inherit results.

The local compliance-test RC package passed the original-license/source/material checks and a modified-agbplay rebuild using only supplied source/support materials. The unchanged new core loaded that replacement, matched AAMJ golden PCM, and actual RetroArch loaded it for 500 frames with zero observed core fallback/under/overrun. The test-only modified library is not distributed. A final source-export package must be built from the final committed clean source and re-audited; the compliance-test ZIP is not a published Release asset.

ROM reads and temporary writes used private isolated test inputs/output directories, not user game saves. No raw ROM/state/PCM fixture is committed. Existing v0.2 tag, Release and DLL are preserved. 3x remains unvalidated, GB/GBC audio remains research, and neither a full playthrough nor all-scene BGM/SE correctness is claimed. Current publication uses a new clean repository and preserves the old repository privately. Fresh release checks are recorded separately in RELEASE_MANIFEST.md and the packaged validation-summary.json.
