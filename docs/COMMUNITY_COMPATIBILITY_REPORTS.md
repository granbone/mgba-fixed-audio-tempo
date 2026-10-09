# Community compatibility reports

Use the [GitHub compatibility issue form](../.github/ISSUE_TEMPLATE/compatibility-report.yml). Choose **Compatibility report** from the [public repository’s New issue page](https://github.com/granbone/mgba-fixed-audio-tempo/issues/new/choose).

Report each region and revision separately. Include title, language, game code, CRC32/SHA256, core version/manifest commit, frontend/OS, Fixed Audio setting, speed, BGM/SE results, State Load/Rewind behavior and reproduction steps. For FFTA, include how long native audio remained before recovery; late states may take up to about 18 seconds.

DO NOT upload or link ROM files, BIOS files, savestates containing copyrighted game data, or copyrighted game assets. Short diagnostic text is useful after removing credentials and personal paths; raw dumps and state blobs are unnecessary. Your GitHub username is sufficient: do not include personal contact details, ROM acquisition information or download sites.

A report is an observation pending review. It does not automatically become LIMITED_TEST_PASS. Maintainers review exact identity, core version, reproducibility and audio evidence before updating canonical JSON. Driver detection, Core Option activation and ACTIVE alone are insufficient. Regional similarity never transfers a test result. Include Experimental / Conservative / Disabled, and back up saves/states before testing an unverified game.

Suggested labels (not created by this phase):

| Label | Purpose |
|---|---|
| compatibility | Release-specific compatibility evidence |
| community-report | User observation pending review |
| bug | Reproducible behavior requiring investigation |
| audio | BGM/SE, tempo, pitch or audio output |
| state-load | Save/load recovery issue |
| rewind | Rewind/recovery issue |
| 3x | Unvalidated speed feedback; not a supported target |

The issue form cannot prevent every unwanted attachment. Maintainers should remove prohibited uploads/links rather than adding them to research inputs or the database. No community report changes release certification automatically.
