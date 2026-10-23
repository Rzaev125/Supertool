# HDR photographs to SDR design

User requested HDR support and stronger resize verification, and chose HDR → SDR for ordinary displays. Work only in D:/IT/Project/Supertool. This increment is local 0.14, no publication, install or channel changes.

## Supported input and output

Accept single-frame AVIF and HEIC with unambiguous CICP/nclx BT.2020 primaries (9), PQ (16) or HLG (18), 10/12-bit source, matrix0/RGB or9/BT.2020 non-constant luminance, no ICC conflict. Convert to existing 8-bit sRGB outputs through existing bridges. Reject unsupported/missing/ambiguous HDR metadata, ICC+HDR combinations, gain maps, auxiliary/depth content and unsupported transforms. Existing SDR behavior, module requirements, EXIF/date scope and output ownership stay unchanged. HDR output and video HDR are outside this photograph increment.

## Chosen implementation

Existing pinned AVIF/HEIC decoders produce already transformed 16-bit RGB(A) PNG. HEIC validation already reads metadata before decode. AVIF initial ordinary decode supplies its validated diagnostic metadata; only HDR requires a second16-bit decode to a different private filename. Keep the ordinary8-bit SDR path unchanged. Validate metadata in both HDR decode logs and refuse ignored transforms. No new decoder/module/background process. Extra HDR decode is a deliberate speed tradeoff; optimize later only after measuring.

A separate native worker-only WIC helper reads raw64-bit RGBA without applying ICC/gamma conversion. Retain at least16-bit decoded precision until tone mapping. PQ EOTF follows BT.2100. HLG uses inverse OETF plus luminance-dependent OOTF at1000cd/m2, gamma1.2 and black0. Convert linear BT.2020 to linear BT.709 with D65 matrix; clip negative out-of-gamut channels. Units become relative to100cd/m2 SDR reference. Two row-streamed passes: estimate global maximum positive RGB channel excluding alpha0 (minimum peak1), then apply the Hable curve to maximum RGB and scale RGB together, normalized by the image peak. Convert resulting linear sRGB to nonlinear sRGB and8-bit; alpha is linear coverage, never PQ/HLG decoded, rounded16→8. Transparent pixels do not set exposure. Partial-alpha pixels participate without premultiplying tone-map color. This is a documented global tone map, not HDR fidelity or local contrast enhancement.

Use transfer LUTs for16-bit channel codes and a bounded sRGB encoding LUT; per-row storage and small fixed tables, no application-owned full-frame buffer. Decoder/WIC may retain their own image buffers. Keep40M pixels,32768 side,1GiB file limits. Decode/tone-map only during conversion; source remains read locked, all intermediates inside the already owned work directory; never overwrite foreign files. A newly created private normalized PNG then goes through the existing WIC resize/encoder exactly once with no HDR metadata and no second color transform.

## Verification

Resize: independent area-weighted premultiplied RGBA reference, whole-image comparison with bounded rounding tolerance; transparent and opaque regions and asymmetric colors. Mutation checks must reject a horizontal mirror, reversed alpha layout and missing transparency even at unchanged dimensions.

HDR: real PQ/HLG10/12-bit AVIF/HEIC fixtures from known16-bit charts; inspect decoder16-bit samples independently. Pure-Python double-precision BT.2100/Hable/matrix reference for all pixels, including black, several neutral luminances, saturated colors and alpha0/partial/opaque. Prove output differs from simple8-bit clipping, metadata is removed, output is SDR, transforms and resize happen once, bridges/white blend/source hashes/temp cleanup remain correct. Test unsupported metadata and unavailable modules. No new Python dependencies.

Builder may settle exact helper API names and reasonable numerical tolerance after quantization experiments, but must document tolerance and show mutations distinguish wrong transfer/missing tone map. Existing tests incorrectly treating every HDR tag as refusal must be narrowed to unsupported8-bit/malformed cases or changed to valid supported fixtures.
