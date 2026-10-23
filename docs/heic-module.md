# Portable HEIC module

ShiftWheel uses official MSYS2 UCRT64 Windows x64 packages: **libheif 1.23.6-1**, **x265 4.3-1**, and **libde265 1.1.3-1**. No system codec, Microsoft Store extension or MSYS2 installation is required. The conversion worker loads the metadata API only while converting; the tray process does not load these codecs.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/fetch-heic.ps1` to download verified packages and assemble `tools/modules/heic`. The script pins every archive and SHA256 in `scripts/heic-packages.json`; it does not use an unversioned download. `scripts/heic-runtime-files.json` is the PE import dependency closure of libheif and the three command line tools. The 34 binary files occupy 67,679,473 bytes. Build packaging copies this directory beside ShiftWheel.exe as `modules/heic`.

## Supported behavior

- Static, 8-bit SDR HEIC/HEIF (local0.14 also supports10/12-bit BT.2020 PQ/HLG to SDR; see [HDR images](hdr-images.md)), including alpha, and conversions to/from PNG, JPEG, BMP, TIFF, WebP and AVIF.
- A single top-level image; attached thumbnails and the alpha auxiliary image do not count as additional frames.
- Container rotation (`irot`), reflection (`imir`) and clean-aperture crop (`clap`) are applied by libheif before PNG output. Codec padding is cropped correctly. Local0.15 applies original EXIF Orientation1–8 only when the primary image has neither irot nor imir. Property presence, including identity, takes precedence; crop alone does not suppress EXIF. Invalid/duplicate EXIF orientation is refused. See [selected photo metadata](photo-metadata.md).
- ICC profiles are retained by the decoder and required by WIC color management before normalization to sRGB. Without ICC, only sRGB/unspecified NCLX primaries and transfer are accepted. Export declares sRGB NCLX (1/13/6), 8-bit 4:4:4, and drops source metadata.
- Compact/balanced/high presets use HEVC quality 60/80/95 and the fast encoder preset. Encoding thread pools follow the resource setting; frame parallelism and decoding tile parallelism are one. Image maximum dimension uses the shared aspect-preserving downscaler and never enlarges.
- Unicode filenames are handled by Win32 mapping and the native MSYS2 executables.

## Explicit limitations

Unsupported HDR metadata/ICC conflicts, high-depth SDR input, sequences, multiple top-level images, premultiplied alpha, depth maps and other auxiliary images (including HDR gain maps) are refused, with an actionable error. The app does not silently choose the first frame or discard a gain map. Normal thumbnails are allowed.

Input limit is 1 GiB; decoded dimensions are limited to 40 million pixels and 32768 on either side. Original `ispe` and transformed dimensions are both checked before pixel decoding. Metadata parsing uses libheif's security limits tightened to 40 MP, 4 MiB profiles, 512 MiB per block and 1 GiB total. Pixel decoding retains the pinned tool's default safety limits. Decoder/encoder timeouts and cancellation remain in the existing Windows Job Object process boundary.

## Provenance and licenses

Primary sources:

- [MSYS2 libheif package](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-libheif)
- [Official MSYS2 package repository](https://repo.msys2.org/mingw/ucrt64/)
- [libheif 1.23.6 source and public API](https://github.com/strukturag/libheif/tree/v1.23.6)
- [MSYS2 build recipes and patches](https://github.com/msys2/MINGW-packages)
- [x265 4.3 source release](https://github.com/Multicorewareinc/x265/releases/tag/4.3)

Main archive SHA256:

| Component | SHA256 |
| --- | --- |
| libheif 1.23.6-1 | `1dc60f9d1218d79ab56d3b3ce44b9bcc98e411f6e316db38acb33e9fe4ba21b9` |
| x265 4.3-1 | `40dd235ba4294266ecad0eec661fae7db25dfe9df2ee9a7c3e07b0d079ae2034` |
| libde265 1.1.3-1 | `2d722373ff26d4f281cbfa49e4518ee00aa5d9ccbba0e6cbde7cade50124e9fa` |

The included `licenses/` directory retains package copyright/license notices. libheif and libde265 are LGPL; the MSYS2 libheif binary also links GPL x265 and x264 codecs. This module is not a permissively licensed codec bundle. A redistributed release must include the corresponding license notices and satisfy source-distribution obligations for these binaries and their dependencies. The pinned package manifest plus MSYS2 build recipes identify those dependencies. HEVC patent licensing is a separate distribution consideration.

The official ImageMagick 7.1.2-34 portable build was tested during selection, but exposes HEIC as read-only; it is not shipped as this module. Official libvips Windows bundles omit HEVC encoders/decoders. The MSYS2 module was selected after successful local HEVC encode/decode and Unicode/alpha checks.

## Validation

`tests/heic_tests.py --exe <path-to-ShiftWheel.exe>` creates real PNG/HEIC fixtures and checks round trips, alpha, Unicode, all raster bridges, quality/resize, rotation/reflection/crop, ICC normalization, damaged input, excessive dimensions, unsupported8-bit HDR/multiframe refusal and missing-module errors. `tests/hdr_image_tests.py` independently checks supported10/12-bit PQ/HLG color, alpha and guards. Tests use Python/Pillow only to create and inspect fixtures; the app does not require Python.
