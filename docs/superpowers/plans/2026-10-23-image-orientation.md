# EXIF orientation and capture-date implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline, task by task.

**Goal:** Normalize the eight EXIF orientations in JPEG/TIFF and static WebP, and offer explicit opt-in capture-date retention for JPEG/TIFF to JPEG/TIFF conversions.

**Architecture:** WIC transforms pixels once before resizing/encoding. Static WebP validates its bounded EXIF directory and passes an explicit orientation to WIC after a no-autorotate decode. The metadata choice is part of immutable worker options and durable queue snapshots. No new engine or background work.

**Tech Stack:** Existing C++20, Win32/WIC, pinned FFmpeg, Python/Pillow test fixtures.

**Spec:** Approved stage 6 in `docs/superpowers/plans/2026-10-23-next-improvements.md`. This is the next bounded increment, not completion of HDR or full metadata preservation.

## Global Constraints

- Work only in D:/IT/Project/Supertool; preserve source and foreign output/temp files.
- Keep image limits 40M pixels and 32768 per side; bounded WebP metadata 4 MiB.
- No new dependencies, idle polling, or relaxation of ICC/HDR/animated metadata checks.
- HEIC/AVIF container transforms and EXIF conflicts retain their existing restrictions.
- Default removes EXIF/XMP; an opt-in copies only a valid DateTimeOriginal (19 ASCII characters) between JPEG/TIFF. No GPS, author, camera, MakerNote, thumbnails, subsecond or timezone promise.
- Date choice has no effect on other input/output combinations; UI and docs state JPEG/TIFF scope explicitly.
- Preserve 0.11 preview/stable publication; this increment is a local tested branch, no release is required by this task.

## Review Focus

- Ambiguous/duplicate/malformed orientation must fail instead of silently choosing a different view.
- Transpose/transverse and unequal DPI axes must follow swapped pixel axes.
- Bridges to WebP/AVIF/HEIC/GIF must not apply EXIF twice or resize twice.
- Invalid capture dates and oversized/unexpected metadata variants must not be copied.
- Legacy SWQ1/SWQ2, absent INI field, invalid combo selection and immutable snapshots retain defaults and validation.

### Task 1: Normalize EXIF pixels

**Files:** src/image_converter.h/.cpp, src/webp_converter.cpp, tests/image_orientation_tests.py, tests/conversion_tests.py, CMakeLists.txt.

- [ ] Add real fixtures covering all eight JPEG/TIFF/WebP orientations against independently transformed decoded pixels, alpha, resize, bridge output, malformed EXIF, source hashes and no temp leaks.
- [ ] Run new Python test against current executable. Expected: fails on orientation 2 because current product refuses it.
- [ ] Implement validated WIC orientation and rotation before resize; swap DPI axes. Explicit override is worker-internal and only for validated WebP orientation.
- [ ] Update the old TIFF rotation-refusal case to verify dimensions.
- [ ] Build and run images, conversion_formats, conversion_animated_images, conversion_heic, conversion_avif and new image_orientation. Expected: all pass.
- [ ] Commit implementation and record task completion.

### Task 2: Explicit capture-date choice

**Files:** src/options.h/.cpp, src/job_store.cpp, src/control_panel.cpp, src/image_converter.cpp, tests/options_tests.cpp, tests/job_store_tests.cpp, tests/panel_tests.cpp, tests/image_orientation_tests.py.

- [ ] Add failing persistence/UI/date tests. Expected: old parser rejects image_metadata=capture_date and old executable loses the date.
- [ ] Add ImageMetadata {Remove, CaptureDate}, INI image_metadata=remove|capture_date with strict validation; snapshot same field. Save SWQ3 and read SWQ1/2 defaults.
- [ ] Add stable-ID settings combo with text clearly limiting capture date to JPEG/TIFF, scrollable layout, defaults, save/restore and invalid-selection refusal.
- [ ] Copy only a bounded, valid ASCII EXIF DateTimeOriginal via WIC reader/writer for direct JPEG/TIFF pairs. Strip remains default; orientation tag is never copied.
- [ ] Run options, job_store, panel_contract and image_orientation. Expected: pass; commit and record.

### Task 3: Verify and document

**Files:** README.md, docs/image-orientation.md, docs/acceptance-0.12.0.md, docs/review-0.12.0.md, roadmap; version files when identifying local 0.12 preview build.

- [ ] Run full CTest suite sequentially from D with workspace-local TEMP/TMP. Expected: all pass, no source C paths in active CMake cache.
- [ ] Record exact coverage, metadata scope, unsupported HEIC/AVIF EXIF/container cases, and manual Explorer/VM limitations. Keep metadata roadmap partial where full transfer is unimplemented.
- [ ] Commit; obtain one fresh whole-branch review, fix Important/Critical once with RED/GREEN + full suite, defer minors explicitly.
- [ ] Finish with exact deletion status and local build/test result. No claim that old C was deleted if its atomic rename remains blocked.
