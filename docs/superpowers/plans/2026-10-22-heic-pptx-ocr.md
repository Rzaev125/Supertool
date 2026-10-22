# HEIC, presentations and PDF OCR implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development; root owns shared integration and final verification.

**Goal:** Ship native ShiftWheel 0.4.0 with HEIC import/export, PPTX presentations, and scanned PDF to TXT in Russian, English and Azerbaijani.

**Architecture:** Keep the C++20 Win32 shell, immutable job options, sequential workers, below-normal-priority engine processes, and exclusive output publication. Add portable libheif/x265 and Tesseract with bundled eng/rus/aze models; reuse LibreOffice/PDFium for presentations.

**Spec:** User explicitly authorized continuing the roadmap with HEIC/PPTX/OCR and chose TXT only plus Russian/English/Azerbaijani. Native language/architecture and local conversion are existing requirements. Engine choices and fixed OCR300DPI are implementation decisions. No searchable PDF output in this release.

## Shared interfaces and boundaries

- Append `Format::Heic`, `Format::Pptx`, `Format::Odp`; append `Category::Presentation` without changing old enum identities. HEIC/HEIF input aliases; PPTX/PPT/ODP inputs. Presentation targets PDF/PPTX/ODP/PNG/JPG; images export every slide into one folder.
- `convert_heic(input, output, format, work, const ConversionOptions&)` in heic_converter.*. Root dispatches before AVIF/WebP/WIC whenever either side is HEIC. Work and output already belong to the outer converter staging. All bridges preserve options, color/alpha and resource limits.
- `convert_presentation(input, output, format, work, const ConversionOptions&)` in presentation_converter.*. Uses a private macro-disabled LibreOffice profile. PNG/JPG route via internal PDF and existing convert_pdf; root checks both module requirements.
- Add `ConversionOptions::pdf_ocr=true` and `std::string ocr_languages="eng+rus+aze"`. Serialization remains version1 and missing new keys load defaults. Accept nonempty unique combinations of installed whitelist eng/rus/aze, never arbitrary paths/options. Snapshots carry OCR configuration.
- PDF agent owns pdf_converter.* and new ocr.*. Existing convert_pdf signature remains. OCR only on pages lacking meaningful embedded text; preserve page order in mixed PDFs. Fixed render300DPI subject to current bounded raster constraints. Tesseract data and executable live under modules/ocr. Cancellation uses existing Windows Job hierarchy.
- Root owns policy/options/panel/engines/converter/CMake/README/packaging. New settings: scan OCR toggle and bounded language choices. Add Presentation to configurable wheel categories. Existing selected image format subsets remain unchanged; HEIC can be enabled in settings.

## Tasks and checks

- [x] Routing/options/UI: RED new policy and settings tests; implement format/category mapping, aliases, image/presentation output naming, OCR snapshot/parser, presentation wheel settings and OCR controls. GREEN targeted tests.
- [x] HEIC: trusted portable module with recorded digest/license; real static encode/decode tests, HEIF alias, resizing/alpha/ICC/orientation and Unicode; reject unsupported sequence/HDR/oversized/malformed input, preserve source/output. No silent first-frame-only behavior.
- [x] PPTX: verify LibreOffice filters from primary documentation; test real multi-slide PDF and all-slide images, PPTX/ODP roundtrip, source/foreign output preservation and malformed/missing engine errors.
- [x] OCR: trusted portable Tesseract and official models; test actual raster PDFs for all three languages, ordered text/scanned pages, blank pages, disabled OCR, missing data and cleanup. Digital-text PDF remains independent of OCR module availability.
- [x] Integration/review/release: full test suite, independent review and scoped fixes, native UI checks as available, version/docs/previews, portable archive integrity+hash and launch. Preserve user settings and existing releases.

## Review focus

Color/alpha/orientation and HEIC primary/auxiliary images; slide loss or hidden-source image drop; mixed PDF page order and missing OCR languages; preservation of originals/foreign outputs under cancellation; portable binary/model supply and missing-module errors. Native Shift-drag and mixed physical DPI remain separate manual acceptance, not implied by synthetic tests.

Git is unborn and .git read-only; use current isolated workspace, no commits/worktree mutation. No system-wide dependency installation. New modules execute only during conversion; packaging excludes personal INI.

## Implementation decisions and evidence

- HEIC module: upstream ImageMagick and libvips ready-made Windows builds lack HEVC encoding. Use official MSYS2 UCRT64 libheif + x265 dependency closure, recorded package digests and licenses, instead. Scope remains read/write.
- Migration: legacy0.3.0 config with exact default six image formats gains HEIC. Custom subsets/order remain unchanged. Presence of new OCR keys distinguishes an explicitly saved0.4.0 configuration, so hidden HEIC stays hidden. Cost if undesired: user can hide it once.
- Routing/settings RED observed (unknownHEIC/PPTX/OCRkeys), targeted policy/options/panel tests GREEN after implementation. Additional legacy-wheel migration RED observed before fix.
- Final Release /W4 build clean. Full CTest 23/23 PASS, 139.40 seconds; targeted new/integration tests 7/7 PASS before final placeholder guard. Native OCR settings visually verified, HEIC and presentation Direct2D previews inspected.
- Independent review found placeholder-only text could bypass OCR; U+FFFD/U+FFFE/U+FFFF now excluded. Raster+placeholder integration is covered, but pinned PDFium itself removes the fixture's U+FFFD, so that integration test does not demonstrate RED for the defensive guard. Valid-text-plus-scan regions on one page remain explicitly documented outside automatic OCR scope.
- Portable 0.4.0 archive: 629209273 bytes, 21170 unique entries; all CRCs, exact EXE/docs/new-module contents and UTF-8 verified. No personal INI included. SHA256 CF941A24284D6FC2CA06D6BA7C352275A446138CF7761804B6A7A6CC3DC02F07. Packaged application launched with preserved prior settings; old releases retained.
