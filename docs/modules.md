В 0.9 доступна базовая сборка и установка отдельных компонентов: [инструкция](modules-0.9.md).

# Conversion modules

Version 0.4.0 adds portable HEIC read/write and local PDF OCR; presentation
conversion reuses LibreOffice Impress and PDFium. See
[HEIC module](heic-module.md), [OCR module](ocr-module.md), and
[presentation support](presentation-module.md) for versions, hashes, limitations
and license provenance. The OCR model selection exposed in settings is
English/Russian/Azerbaijani; aze also requires the bundled aze_cyrl model.

WebP, Opus, AAC and MOV reuse the bundled FFmpeg; TIFF uses the Windows WIC
codec. No additional binaries or resident services were added in 0.2.3.
WebP export uses libwebp lossless, compression level 4, effort 75 and BGRA.
Before decoding, the RIFF container is checked for one frame, bounded chunk
lengths, pixel dimensions, EXIF orientation and ICC presence. FFmpeg preserves
the ICC in its intermediate PNG; WIC requires that profile and normalizes to
sRGB. ICC loss fails the job instead of changing colors silently.
WebP input is limited to 1 GiB / 40 million pixels; each FFmpeg image step has
a 300-second timeout. WebP export is limited by libwebp to 16383 per dimension.
TIFF output is WIC ZIP-compressed 8-bit RGBA, with sRGB normalization and one
page only. Opus output is stereo 48 kHz / 128 kbps, AAC is 192 kbps ADTS, and
MOV uses H.264 CRF 23 / AAC 192 kbps with faststart.
Implementation references: [WebP RIFF specification](https://developers.google.com/speed/webp/docs/riff_container),
[FFmpeg codec options](https://ffmpeg.org/ffmpeg-codecs.html#libwebp),
[WIC TIFF codec](https://learn.microsoft.com/en-us/windows/win32/wic/tiff-format-overview).

AVIF: official libavif 1.4.2 Windows x64 tools from
[the upstream release](https://github.com/AOMediaCodec/libavif/releases/tag/v1.4.2).
`windows-artifacts.zip` SHA-256 matches the GitHub release asset digest:
`cb2d9fea43dcbab1d0707e3b37eb7b08070ad2fb60a2c188c39ec12382c0484a`.
Included: avifenc.exe, avifdec.exe, and the signed Microsoft VCRUNTIME140.dll
14.44.35211.0 already bundled with the LibreOffice module. The Windows 10/11
Universal CRT is a system dependency. AVIF tools use aom 3.14.1, dav1d 1.5.3,
libyuv 1924, libjpeg-turbo 3.1.4.1, libpng 1.6.58, zlib 1.3.2 and libsharpyuv
from libwebp 1.6.0; license and patent notices are in `modules/avif/licenses`.
They run only for AVIF tasks, with two codec threads and below-normal priority.
Encoding: quality 80, alpha quality 100, speed 6, 8-bit YUV 4:4:4, CICP 1/13/6.
Decoding: at most 40 million pixels / 32768 per dimension, 8-bit output. Input
for AVIF jobs is limited to 1 GiB. Animated images, transforms and HDR are
rejected before publication. Embedded ICC profiles are converted to sRGB using
WICColorTransform and must survive decoding; otherwise the job fails. Without
ICC, only sRGB or unspecified CICP primaries/transfers are accepted. Output
metadata is stripped after color conversion. The fixed avifdec 1.4.2 diagnostic format is
validated because its CLI has no machine-readable metadata output.

Verified local FFmpeg build: 9.0.2 essentials, archive SHA-256 `4705843ccaaf54257c16ad90f3e952ece33c17df964ecf7bfdbb0f49c7171077` (matched the provider's checksum). LibreOffice extraction also includes `help` and the redistributable DLLs from `System64` copied into `program`, so it does not rely on an installed VC++ redistributable.

The resident application is native C++/Win32. Modules are separate processes or DLLs loaded only by a conversion worker. No Python runtime is shipped.

Layout next to Supertool.exe:

```
modules/ffmpeg/ffmpeg.exe
modules/ffmpeg/ffprobe.exe
modules/libreoffice/program/soffice.com
modules/pdfium/pdfium.dll
modules/avif/avifenc.exe
modules/avif/avifdec.exe
modules/avif/vcruntime140.dll
modules/heic/libheif.dll
modules/heic/heif-enc.exe
modules/heic/heif-dec.exe
modules/heic/(bundled dependent DLLs and licenses)
modules/ocr/tesseract.exe
modules/ocr/(bundled dependent DLLs and licenses)
modules/ocr/tessdata/{eng,rus,aze,aze_cyrl}.traineddata
```

- FFmpeg: Gyan release essentials, GPLv3 build. Binaries, build configuration and license originate from https://www.gyan.dev/ffmpeg/builds/ (linked by https://ffmpeg.org/download.html). Keep the included LICENSE and README with the binaries. Upstream source: https://github.com/FFmpeg/FFmpeg ; build/source information: https://www.gyan.dev/ffmpeg/builds/#about-these-builds . This is a local development distribution, not a prepared public release with a corresponding-source bundle.
- LibreOffice 26.8.1 x64: official signed MSI, extracted with `msiexec /a ... /qn TARGETDIR=...`, without a normal installation. Download: https://download.documentfoundation.org/libreoffice/stable/26.8.1/win/x86_64/LibreOffice_26.8.1_Win_x86-64.msi . SHA-256: `c6298b10cfa1748bcbedad94c62ce771e240e5021bfd6d7a967cd571b04b5312`. Copy program, share, presets, readmes, LICENSE.html, license.txt, NOTICE and CREDITS.fodt. Source and licenses: https://www.libreoffice.org/about-us/licenses/ . Each job uses a private profile with macros disabled.
- PDFium 156.0.8076.0: native x64 DLL from the available pypdfium2 5.14.0 runtime distribution. Copy pdfium.dll, version.json and the distribution's BUILD_LICENSES directory. BSD and third-party notices are in modules/pdfium/licenses. Upstream: https://pdfium.googlesource.com/pdfium/ ; binary project: https://github.com/bblanchon/pdfium-binaries . PDF pages render at 144 DPI with a white background.

Development: put the module folders in `tools/modules`, then copy them to `build/Release/modules` for integration tests. `scripts/package.ps1` copies these modules into the portable release. Always obtain modules from their trusted upstream providers and retain license notices when redistributing.

Portable modules occupy disk space but do not run in the background. LibreOffice is substantially larger than the native shell. The application uses no network service for conversion.
