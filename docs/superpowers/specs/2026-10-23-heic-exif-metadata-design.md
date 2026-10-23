# HEIC EXIF orientation and selected photo metadata — local0.15

User approved the next development increment: HEIC EXIF orientation and expanded metadata preservation. Exact D:/IT/Project/Supertool checkout. Existing0.14 archives/installers, user settings, channels and release trust stay unchanged; no new publication/installer required.

## Scope and decisions

HEIC/HEIF EXIF Orientation1–8 applied once before resize for SDR and supported HDR→SDR. Existing libheif normalizes clap/irot/imir during decoding and resets PNG EXIF orientation, so original EXIF must be inspected first. When primary HEIF has an irot or imir property (including explicit identity), container orientation is authoritative and EXIF does not apply again. Without either orientation property, apply EXIF to already cropped pixels. Do not infer property presence from dimensions. Duplicated or invalid EXIF orientation is refused even if container orientation exists. Reject duplicate container rotation/mirror properties rather than guess.

Builder choice, disclosed in chat while optional preference unanswered: add a third `photo_details` mode for date, author and copyright, no GPS. Existing `remove` default and legacy `capture_date` JPEG/TIFF→JPEG/TIFF behavior stay identical. New mode supports JPEG/TIFF/HEIC source→JPEG/TIFF output. Other source/target formats retain metadata removal. Do not copy whole EXIF/XMP/MakerNote, thumbnail, dimensions, Orientation, GPS, device serials or ICC into the result. ICC continues to affect pixels through the existing color path. Camera/lens fields and all-format EXIF export remain later increments.

Fields: DateTimeOriginal36867 (existing strict calendar/string19), Artist315 (single text, max256 bytes), Copyright33432 (single text, max512 bytes), XPAuthor40093 (Windows Unicode author, max1024 bytes incl UTF16LE NUL, valid surrogate pairs). Preserve bounded non-control original text bytes, no guessing/re-encoding of legacy byte strings; reject embedded NUL/control/unterminated strings by omitting that optional field. Valid XPAuthor retains UTF16LE exactly. Invalid/duplicate optional fields are omitted; malformed directory/offset structure or ambiguous orientation refuses conversion. No new preservation of timezone/subseconds in this increment.

## Architecture

Extend image_exif helper with shared bounded file/span TIFF reader and `PhotoExif` semantic values. JPEG scan still stops before SOS, max65536 marker steps; TIFF root/Exif IFD max4096 entries, no arbitrary recursion or IFD1/GPS traversal. Read optional selected values only for new mode. HEIC metadata stays capped4MiB, one EXIF block and original4-byte TIFF-header offset validated. Keep all existing1GiB/40MP/32768 limits and locks/private work/no-overwrite behavior.

HEIC dynamic pinned1.23.6 ABI additionally reads primary item ID/property types to detect irot/imir directly. Return source orientation/PhotoExif in validation result. After SDR or HDR decode, use existing `convert_image` orientation override for exactly one orientation/resize; preserve all raster bridges. Add optional metadata override for normalized HEIC input. New mode on JPEG/TIFF uses original bounded parser values. Existing capture-date WIC read path remains unchanged.

WIC writes only individual validated allowed tags for JPEG/TIFF. No opaque EXIF injection or post-publication mutation. New enum value2 is serialized in option snapshots and SWQ3; current readers validate it, old binaries reject new-mode queues/settings. Document downgrade using separate backed-up INI/queue. No SWQ format expansion required, existing fields retain meaning. UI label names supported source/target pair; third choice names the fields and excludes GPS.

## Verification and release boundary

Independent real fixtures attach raw EXIF after encoding with pinned libheif API, avoiding encoders automatically translating EXIF to container transforms. All8 orientations, raw decoder comparison + independent pixel transforms, alpha, resize, every output bridge, existing container rotate/mirror/crop combinations and explicit identity orientation. Include supported HDR/ICC paths, both TIFF byte orders, duplicate/invalid type/value/offset/large/multiple-block refusal. Source hashes and work cleanup checked.

New metadata mode tests all JPEG/TIFF/HEIC→JPEG/TIFF pairs, date+ASCII/extended-byte author/copyright and Unicode XPAuthor; invalid/duplicate optional fields, malformed nested IFD, legacy modes/defaults and all unsupported outputs remove metadata; GPS/MakerNote/XMP/thumbnail/Orientation absent. Settings snapshots, persisted queue/new mode and panel choice tested. Baseline/profile then full sequential CTest and standalone release-version guard, one final fresh read-only review; one confirmed Important/Critical fix pass. Local0.15 version/docs prepared before packaging-policy tests. No performance or manual-Explorer/VM/signature claims.

User preference confirmed: date, author and copyright without GPS.
