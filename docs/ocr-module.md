# Local PDF OCR

PDF to TXT first extracts each page's existing text. When a page has only
whitespace or replacement/noncharacter text, it is rendered by PDFium at 300 DPI
and recognized by a short-lived Tesseract process. English (`eng`), Russian
(`rus`) and Latin Azerbaijani (`aze`) are enabled by default and can be selected
individually. The official `aze` model also loads `aze_cyrl`; that companion is
included as an internal dependency.

Embedded and recognized pages retain their original order, with a form-feed
between pages. Completely white pages keep their place without starting OCR;
a wholly blank/unreadable document fails without publishing an empty result.
An existing text layer is preserved as supplied by the PDF: a page that combines
valid embedded text with a scanned image is not partially OCRed. Recognition
can make mistakes, especially on handwriting, low resolution, unusual layouts,
skew, or poor contrast. TXT does not preserve page layout, tables or formatting.

The source PDF is never modified. OCR runs locally without uploading documents,
and only receives our rendered PNG through inherited stdin. UTF-8 text returns
through a separately opened stdout file; stderr stays in a private diagnostic
log. Windows wide-path handles and an ASCII relative `tessdata` path make both
Unicode document names and a Unicode portable application directory work.
Private page files are deleted after each page and on failures/cancellation.

Limits: 1 GiB PDF, 1,000 pages, 40 million pixels and 32,768 pixels per side at
300 DPI, 120 seconds per OCR page. The one-hour document deadline is checked
before each textless page; an in-flight page may finish after that deadline.
Rendering memory is released before starting recognition. This selected Windows
build has no OpenMP dependency: recognition uses one worker thread, at Windows
below-normal process priority. It is a CPU scheduling choice, not a percentage
cap. No OCR process runs while the application is idle. Progress advances by
completed PDF pages; it is not an estimated character-recognition percentage.

## Portable runtime

`modules/ocr` contains Tesseract 5.5.3.20260724 and its 33 dependent DLLs, four
`tessdata_fast` models and notices. No Python runtime or system installation is
needed. Missing OCR does not prevent born-digital PDF extraction; a scanned
page produces an actionable message when the engine or selected model is absent.

The Windows binary is the upstream release asset linked from the
[Tesseract installation documentation](https://tesseract-ocr.github.io/tessdoc/Installation.html)
and [UB Mannheim distribution page](https://github.com/UB-Mannheim/tesseract/wiki):
[5.5.3 release](https://github.com/tesseract-ocr/tesseract/releases/tag/5.5.3).
The installer archive SHA-256 matches the GitHub release asset digest:
`bee9e3434bd94fd65387d9be28cd467a41f61b1275383b55b0f59a1331270ae4`.
The installer is unpacked as an archive; it is never executed.

Models come from [official tessdata_fast](https://github.com/tesseract-ocr/tessdata_fast)
at commit `87416418657359cb625c412a48b6e1d6d41c29bd`:

| Model | SHA-256 |
| --- | --- |
| eng | `7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2` |
| rus | `e16e5e036cce1d9ec2b00063cf8b54472625b9e14d893a169e2b0dedeb4df225` |
| aze | `a365310848aecb739f19369cb3831d4660fcd9345d798e91a3042455f9ccc9f0` |
| aze_cyrl | `2e96ab67817819e14a3a2d6250261130457c8e78dde3077841ab391fe003551c` |

Recreate the module from the workspace using
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/fetch-ocr.ps1`.
The script checks pinned hashes, extracts only the listed runtime files,
copies retained notices, and checks the runtime version. Its pinned 7-Zip 26.05
extraction helpers are also unpacked inside `tools`; neither helper installer
is run or included in the resulting module. It does not change PATH or registry.

## Notices and source provenance

Tesseract and models use Apache-2.0. The binary's Leptonica, image/archive,
compression, network and compiler-runtime dependencies have their own licenses,
including GPL/LGPL components. Retained license texts and their source map live
in `licenses` (repository source: `docs/licenses/ocr`). The native runtime file
allowlist and original download URLs/hashes are copied beside it.

The published Tesseract source and Windows build information are available in
[tesseract-ocr/tesseract](https://github.com/tesseract-ocr/tesseract/tree/5.5.3)
and [UB-Mannheim/tesseract](https://github.com/UB-Mannheim/tesseract).
This is the project's local development distribution, not a prepared public
release with corresponding source bundles for every dependency. Preserve notices
and prepare the required source/build material before public redistribution.

Verification: `tests/ocr_tests.py` uses real raster PDFs in all three languages,
including Azerbaijani ə/ı/ş/ç/ö/ü/ğ; mixed text/image/blank pages; replacement-only
text layers; source preservation; collision refusal; disabled/missing OCR;
language selection; and Unicode source/output/application paths.
