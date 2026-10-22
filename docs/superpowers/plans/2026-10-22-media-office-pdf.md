# Media, Office and PDF implementation plan

> **For agentic workers:** Use superpowers:executing-plans for inline implementation and a final fresh-context review.

**Goal:** Extend the approved native converter with working video, audio, DOCX, XLSX and PDF conversions.

**Architecture:** The resident Win32 application selects a route from a shared catalog. A separate worker invokes FFmpeg or LibreOffice, or loads PDFium only for a PDF job; a Windows job object owns the complete process tree. Outputs are staged beside the original and published without replacing existing paths.

**Tech stack:** C++20, Win32, WIC, Direct2D; portable FFmpeg, LibreOffice, PDFium.

**Spec:** ../specs/2026-10-22-shift-wheel-design.md plus the user's explicit request to add all five categories.

## Constraints and decisions

- Keep one conversion active, below-normal process priority, no polling or engines resident at idle. FFmpeg uses two codec/filter threads.
- Preserve originals and existing destinations. Local regular files only; reject reparse points and network paths.
- Video outputs MP4, MKV, WebM, MP3, WAV; audio outputs MP3, WAV, FLAC, M4A, OGG. Export the first video/audio streams; disclose that additional streams/subtitles are omitted.
- Writer inputs DOCX, DOC, ODT, RTF; outputs PDF, DOCX, ODT, RTF, UTF-8 TXT. Calc inputs XLSX, XLS, ODS; outputs PDF, XLSX, ODS, CSV. CSV exports every sheet into a folder, with values rather than formulas/formatting.
- PDF outputs all pages to a PNG/JPG folder at 144 DPI or extracts UTF-8 text. No OCR or PDF-to-editable-Office claim. Reject encrypted/empty/invalid PDFs; cap pages at 1000, raster pages at 40 megapixels, text at 10 million characters per page.
- Modules live beside the executable, are not installed globally, and have their licenses included. Missing modules produce a clear error; no implicit downloads at runtime.
- Manual Explorer dragging and mixed-DPI behavior remain a separate unverified check.

## Review focus

- Multi-page PDF / multi-sheet spreadsheet: test that later pages and sheets survive.
- Cancellation during an external child: prove descendants exit and private staging is removed without touching foreign files.
- Unicode and spaces in paths: run real conversions with both.
- Wrong or corrupt source contents / missing engine: fail without publishing a partial result.
- Existing destination / temporary path: refuse without truncation; output-directory publication must also be exclusive.

## Tasks

### 1. Catalog and adaptive wheel
- [x] Extend policy tests first; observe old implementation reject Office/media.
- [x] Add Category, route list, format label/caption/ID, folder-output policy in policy.h/.cpp; keep image checks narrow.
- [x] Use the route vector for hit testing, rendering and drop validation; revalidate the same source and route at Drop. Exercise 4/5 sectors and changed data objects in OLE tests.

### 2. Conversion engines and process lifetime
- [x] Add engine paths and process runner in engines.h/.cpp; add converter.h/.cpp orchestration and pdf_converter.h/.cpp.
- [x] Extend controlled worker cancellation fixture to launch a descendant that holds a nested file open; observe old cancellation fail.
- [x] Kill/wait the entire job, remove only exclusively created staging without traversing reparse points. Keep image CLI compatible; allow explicit target ID for folder outputs.
- [x] Run engines with quoted argv, a per-job LibreOffice profile (macros disabled), fixed filters, bounded FFmpeg threads; publish only validated nonempty results.

### 3. Real fixtures, packaging and review
- [x] Generate small audio/video, two-page PDF, DOCX and two-sheet XLSX fixtures. Run CLI conversions, inspect media streams, document content, all sheets/pages, negative cases and output preservation.
- [x] Run complete CTest, offscreen wheel render, idle CPU/RAM sample. Update reproducible module preparation and packaging scripts.
- [x] Obtain one final independent code review; address material findings with regression coverage.
- [x] Deliver portable 0.2.0 executable/modules and accurate README/verification notes. Git metadata is read-only/unborn; preserve workspace without commits or branching.

