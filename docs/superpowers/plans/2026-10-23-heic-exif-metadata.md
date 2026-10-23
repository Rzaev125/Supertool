# HEIC EXIF and selected metadata implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline.

**Goal:** Normalize HEIC EXIF Orientation1–8 once, and add explicit date/author/copyright preservation without changing old modes.

**Architecture:** Bounded shared file/span TIFF semantic reader, libheif property-based orientation precedence, existing WIC orientation/resize/individual-tag writer. New metadata mode2 persists through existing snapshots/queue. No new runtime library.

**Tech Stack:** C++20/Win32/WIC/pinned libheif1.23.6, Python/Pillow/ctypes real fixtures.

**Spec:** docs/superpowers/specs/2026-10-23-heic-exif-metadata-design.md. User approved development; user confirmed date, author and copyright without GPS.

## Global constraints

- D:/IT/Project/Supertool local branch, preserve frozen0.14 out files, no installer/publication/channel changes.
- Primary irot/imir presence authoritative, including identity; otherwise original EXIF once after container crop and before resize. Invalid/duplicate orientation refused.
- Remove remains default; legacy capture_date unchanged. New photo_details JPEG/TIFF/HEIC→JPEG/TIFF only: date/author/copyright/Unicode XPAuthor, no GPS/opaque EXIF/XMP/ICC/thumbnail/stale orientation/dimensions.
- Existing locks/private files/no overwrite,1GiB HEIC,40MP/32768,4MiB HEIC EXIF,4096 IFD entries,65536 JPEG marker steps. Optional fields bounded, no arbitrary IFD recursion.
- Local0.15 version and all package docs ready before full tests. One fresh final read-only review and one confirmed Important/Critical fix pass. No performance/manual VM/signature claims.

## Review focus

- Container+EXIF combinations do not rotate twice; identity property must still suppress EXIF, crop alone must not.
- Corrupt/duplicate IFDs and offsets do not select arbitrary orientation or trigger out-of-bounds/unbounded reads; optional invalid fields omitted as specified.
- New mode never leaks GPS/serial/thumbnail/XMP/old dimensions and old modes preserve exact behavior.
- HEIC HDR/ICC/alpha and all output bridges normalize orientation before resize exactly once.
- Worker snapshots/queue/UI retain new mode; old queues load and downgrade limitations documented; legacy source/result/temp safety remains.

### Task1: HEIC orientation and bounded semantic parser

- [x] Baseline focused tests, design builder check completed.
- [x] Add real HEIC fixtures with attached EXIF after encoding: all8, primary transforms/identity/crop combinations, both byte orders, malformed/duplicate orientation (multiple-block/4MiB guards retained, not newly stress-tested); observe current refusal RED.
- [x] Extend image_exif semantic file/span parser and HEIC validation property detection, pass orientation override across SDR/HDR and bridges; unit boundary cases RED/GREEN.
- [x] Profile tests green; commit and ledger.

### Task2: Selected metadata mode

- [x] New mode config/snapshot/queue/panel tests RED; real JPEG/TIFF/HEIC→JPEG/TIFF whitelist and invalid/duplicate/private-field tests RED.
- [x] Add enum2/photo_details, bounded semantic extraction and individual WIC tag output/HEIC metadata override, third UI choice; retain legacy capture_date/default.
- [x] Profile tests green, including unsupported output stripping and old modes; commit and ledger.

### Task3: Local0.15 acceptance

- [x] Update all current version declarations incl CI/manifests, README/START and package docs; historical0.14 evidence preserved; release guard GREEN.
- [x] Warning-free build, full sequential CTest on D-local TEMP/TMP; one fresh read-only final review, fix confirmed Important/Critical once with RED/GREEN and full green suite.
- [x] Final evidence/docs commit, archive owned ledger, retain local branch. No new artifacts/installer/publish.
