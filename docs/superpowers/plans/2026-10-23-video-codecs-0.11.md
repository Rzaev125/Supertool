# Supertool 0.11 Implementation Plan

> Execute inline with superpowers:executing-plans; one final fresh-context whole-branch review. Existing source/preview publication authorization applies. Work in place on codex/video-codecs-0.11, base ee527ee; stable remains 0.7.0.

**Goal:** close the README/WebP blend review items and implement the next approved roadmap item, HEVC/AV1 video.
**Architecture:** C++20/Win32, existing verified FFmpeg 0.9 module, sequential queue and event-driven operation. Extend existing media policy and settings rather than adding formats to the wheel: file container and video codec are separate choices.
**Spec:** approved roadmap 2026-10-23-next-improvements.md stage 6, next video item; review-0.10.0.md deferred items. Builder choices: video_codec default (H.264 in MP4/MKV/MOV, VP9 in WebM), explicit h264/hevc/av1. HEVC/H.264 support MP4/MKV/MOV; AV1 supports MP4/MKV/WebM. Reject incompatible combinations with an actionable message before engine invocation. Audio ignores video codec. Keep 8-bit SDR output, no HDR/tone-map claims.

## Global constraints

- Old INI/options and SWQ1 queue snapshots load with default codec; write SWQ2 with an immutable per-job codec. Invalid keys/enums reject.
- Auto probes actual requested-codec NVENC/QSV/AMF frames, then CPU. CPU-only skips probes. Runtime failure retries CPU once with the SAME codec; cancellation never retries. Owned partial cleanup and destination preservation remain intact.
- CPU HEVC uses bundled libx265 with explicit pool/frame-thread bounds; AV1 uses bundled libaom-av1 (no libsvtav1 in this module), resource threads and row-mt. No new module downloads/background activity. Pins and stable unchanged.
- UI names compatibility; default H.264/VP9 behavior preserved. HEVC MP4/MOV hvc1; AV1 MP4 av01, AAC for MP4/MKV, Opus for WebM. Decoder/filter work stays CPU. Codec support is checked through real output, not encoder listing.

## Task 1: README and blended WebP fixture

- [x] Correct format matrix and all animation-rejection statements in README; retain actual AVIF/TIFF/ICC/EXIF restrictions.
- [x] Construct a real WebP from explicit ANMF chunks: offset subrectangles, alpha 128/64 over an existing alpha canvas, blend enabled, unequal delays/finite loop. Assert every composited pixel independently, not only frame zero.
- [x] Test WebP round-trip frames/delays/loop and no-blend mutant sensitivity. Existing implementation may pass because this is missing coverage, not a confirmed runtime defect. Run conversion_animated_images; commit.

## Task 2: HEVC/AV1 settings and pipeline

- [x] Write tests first: codec INI/worker roundtrip/default/refusal, old/new queue compatibility, UI dropdown persistence, deterministic backend selection/fallback/cancellation and container checks; watch new feature fail.
- [x] Add VideoCodec and UI choice; SWQ2 writes/legacy SWQ1 reads, preserving codec on recovery/retry. Keep existing IDs stable by appending UI control IDs.
- [x] Generalize encoder arguments and requested-codec probes; keep ownership/cancellation behavior. Add real FFmpeg tests for supported containers/presets, complete strict decode, codec tags, audio, resize, duration/frame count, CPU-only/unavailable/runtime fallback, source integrity and incompatible-container refusal. Hardware success claims only for devices actually tested.
- [x] Run relevant tests and real codecs, then commit.

## Task 3: Verify and release preview

- [x] Version 0.11.0, packaging/docs refer to new capabilities; reuse immutable engine 0.9 packages. Full CTest, clean /W4 build, one independent review, one pass of important fixes. Record every deferred item/ruling.
- [x] Build/check core/full ZIP/setup/source, exact inventories/hashes, isolated installer lifecycle; publish source branch/draft PR stacked on 0.10 and preview assets; verify anonymous downloads and remote hashes. Keep stable/main unchanged.
- [x] Persist final results/limits and close this plan; no physical Explorer/DPI/clean-VM/signing claims.

## Shared interfaces / Review Focus

Task1→3 README must describe real supported animation behavior; fixture must genuinely use blend subrectangles rather than encoder-selected full frames.
Task2→3 VideoCodec crosses INI/options, job snapshots/recovery, panel and FFmpeg policy; default and legacy data must agree. Review unsupported container fail-before-run, same-codec CPU fallback, codec-specific AMF flags, x265 CPU bounds, cancellation/partial ownership, stale GUI values and real-output assertions. No accidental module pin or stable changes.
