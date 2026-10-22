# Popular formats implementation plan

> Execute inline with verification-before-completion and one independent final review.

**Goal:** Add WebP/TIFF images in both directions, Opus/AAC audio output and MOV video output to the existing native wheel.

**Architecture:** Reuse WIC, FFmpeg and libavif already bundled. WebP passes through a private PNG and existing WIC color normalization; AVIF/WebP cross-conversion uses the same owned staging directory. No new resident process or dependency. User asked for several popular/modern formats; these five choices and presets are implementation decisions.

**Constraints:** Preserve original files, exclusive output creation, cancellation, low idle usage and existing format IDs. Static images only; reject animated WebP, multipage TIFF, unapplied orientation and invalid files instead of silently discarding content. WebP/TIFF export uses 8-bit RGBA and lossless compression. TIFF ICC and WebP ICC normalize to sRGB. Opus 128 kbps, AAC 192 kbps ADTS, MOV H.264/AAC.

**Review focus:** Cross-conversions with AVIF; alpha and ICC color accuracy; dimensions and malformed RIFF bounds before decoding; six/seven-sector OLE routing; existing output/staging preservation.

- [x] Add failing real conversion and policy tests for all five formats, image pixels/alpha/profile, animation/multipage refusal, and media codecs/duration.
- [x] Implement routes, TIFF WIC encode/decode, bounded WebP validation and FFmpeg PNG bridge, audio/video presets, and wheel defaults.
- [x] Verify conversion tests, all-sector OLE targets and rendered previews; run full CTest.
- [x] Independent review, correct substantive findings, version 0.2.3 documentation, update running portable app and verify archive.

Repository has no commits and .git is read-only; keep changes in the current workspace without worktree/commit operations. Full Explorer Shift-drag and mixed-DPI behavior remain separate manual checks, not claimed by OLE tests.

Completed: conversion_formats RED (unknown output format) then GREEN; final full CTest 12/12 in 63.34 s. Read-only reviewer found no additional actionable issues after palette bounds fix. Packaged binary passed conversion_formats again, and the normal tray instance started successfully (PID 22044). ZIP has 20947 unique entries, CRC passed, key engine/app hashes and UTF-8 instructions match disk. Archive: out/ShiftWheel-0.2.3-win-x64.zip, 583109401 bytes. Copy START.txt verbatim from resources to prevent Windows PowerShell 5.1 ANSI decoding of Russian script literals.
