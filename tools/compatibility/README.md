# Compatibility data generation v2

Canonical JSON is the single source for CSV, XLSX, COMPATIBILITY.md and README statistics. Titles/region/revision come only from the pinned No-Intro text DAT. Exact hashes join driver evidence; related regions never inherit results. Private unmatched reports and original developer evidence stay in ignored build directories.

Run offline from the root:

```text
python tools/compatibility/build_compatibility_db.py
python tools/compatibility/generate_compatibility_outputs.py --xlsx
python tools/compatibility/validate_compatibility_db.py
```

The JSON builder uses sanitized exact-hash analysis, original limited PCM/event acceptance and production static eligibility snapshots. ACTIVE-only observations do not promote a row. A backend rejection is separate from an observed native fallback.

XLSX uses the sole build_compatibility_workbook.mjs author and externally provisioned @oai/artifact-tool. ARTIFACT_TOOL_MODULE may identify its provisioned ESM entry; no dependency tree is committed. Preview PNGs stay in ignored build-public-presentation/outputs/presentation-v03.

Excel forbids slash characters in sheet names. The requested logical “Native Fallback / Unsupported” and “Unknown / Not Analyzed” sheets are named “Native Fallback & Unsupported” and “Unknown & Not Analyzed”. All Games and status subsets have filters and frozen headings/identity columns. Summary formulas count by driver and public status directly from All Games; native worksheet search is available.

The standard-library validator checks every field across JSON/CSV/XLSX, subset memberships, summary formula caches, Markdown and README numbers, DAT identity matching, status/eligibility semantics, privacy and production profile-source hash. Metadata source and definitions are bilingual. Metadata scope and attribution are in compatibility/METADATA_SOURCES.md.

A metadata refresh uses import_public_metadata.py with a full pinned revision and actual acquisition date. It downloads only text metadata, never games. Static eligibility collection requires an explicitly supplied ROM directory and verifies exact hashes read-only; save/state testing uses isolated outputs. Such probes do not prove correct audio.
