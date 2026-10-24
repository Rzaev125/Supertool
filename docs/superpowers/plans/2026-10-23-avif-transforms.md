# AVIF container transforms implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline.

**Goal:** Convert single-frame SDR AVIF with container rotation, mirroring and valid clean-aperture crop into correctly displayed pixels.

**Architecture:** Existing pinned avifdec 1.4.2 applies clap, irot and imir before writing PNG. Supertool validates its complete diagnostic lines and uses the normalized PNG once; WIC handles color, size and output. No manual second pixel transform or new engine.

**Tech Stack:** C++20 / Win32 / WIC, existing libavif 1.4.2 tools, Python/Pillow fixtures.

**Spec:** Approved stage 6 of `2026-10-23-next-improvements.md`, continuing image orientation. This bounded increment completes AVIF container display transforms; HDR, full metadata and HEIC EXIF remain separate.

## Global Constraints

- Work in D:/IT/Project/Supertool in place per user instruction, on a new local feature branch. Preserve original files, foreign outputs and existing verified 0.12 packages.
- No new engine, dependency, idle timer, default resource increase, publication or update channel change.
- Keep 1 GiB input limit, 40M pixel and 32768 side limits; animation/HDR/unsupported color remain refused.
- Container transforms are applied exactly once, before user resize. EXIF/XMP remain removed; no new date retention combinations.
- Non-square pixel aspect ratio/pasp remains refused; invalid or ignored transforms fail rather than silently producing an untransformed image.
- Independent pixel expectations derive from a baseline decoded AVIF and Pillow crop/rotate/mirror, never the tool's transformed output.

## Review Focus

- Rotation+mirror order and crop offsets on non-square asymmetric RGBA images.
- Unknown transform diagnostics, ignored/invalid transform warnings, malformed container properties and PASP must fail closed.
- ICC and alpha survive normalization without a second EXIF transform; rotated bridge outputs and resize retain correct display orientation.
- Existing HDR/animation/color refusals, source hashes, output collisions and temporary ownership remain intact.

### Task 1: Enable validated AVIF transforms

**Files:** src/avif_converter.cpp, src/avif_info.h/.cpp, tests/avif_info_tests.cpp, tests/avif_transform_tests.py, tests/conversion_tests.py, CMakeLists.txt.

- [x] Run baseline conversion_avif and image_orientation from the existing build; expected both pass.
- [x] Add real fixtures for rotations 0-3, both mirror axes and combinations, offset crops and crop+rotation+mirror; independently compare every RGBA pixel and dimensions. Include transparency and source hashes.
- [x] Run against the current EXE; expected failure on the first transformed AVIF due to its current guard.
- [x] Accept only known valid rotation/mirror/crop diagnostic blocks from pinned avifdec, reject unsupported PASP/unknown transforms and warnings that transformations were ignored. Rely on decoder pixels; do not rotate again.
- [x] Add malformed/unsupported refusal fixtures, resize, JPEG/BMP white blend and bridges to TIFF/WebP/AVIF/HEIC/GIF. Preserve existing color and animation refusals.
- [x] Update the old rotated-AVIF refusal case to pixel/dimension acceptance; build and run profile tests. Expected pass.
- [x] Commit implementation and record evidence.

### Task 2: Local version and full verification

**Files:** version files, README.md, docs/avif-transforms.md, docs/acceptance-0.13.0.md, docs/review-0.13.0.md, roadmap.

- [x] Identify the next local build as 0.13.0 consistently; preserve 0.12 out artifacts.
- [x] Run full sequential CTest with D-local TEMP/TMP; expected all pass, build without warnings.
- [x] Document precise coverage, libavif source references, unsupported cases and local-only release status.
- [x] One fresh read-only whole-increment review, one fix pass for confirmed Important/Critical issues, regressions then suite as needed. Record deferred items and commit.
