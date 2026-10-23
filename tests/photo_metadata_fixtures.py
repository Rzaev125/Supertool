"""Real metadata payloads and independent native-container inspection for privacy tests."""
import io
import struct
import zlib
from PIL import Image, ImageCms, PngImagePlugin

XMP_TOKEN = b'SHIFTWHEEL-PRIVATE-XMP-15'
THUMB_TOKEN = b'SHIFTWHEEL-PRIVATE-THUMB-15'
XMP = (b'<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF '
       b'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description '
       b'xmlns:dc="http://purl.org/dc/elements/1.1/" dc:description="' + XMP_TOKEN + b'"/></rdf:RDF></x:xmpmeta>')
XMP_JPEG_PREFIX = b'http://ns.adobe.com/xap/1.0/\0'
STALE_SIZE = (9999, 7777)


def jpeg_segment(marker, payload):
    assert len(payload) + 2 <= 65535
    return b'\xff' + bytes([marker]) + struct.pack('>H', len(payload) + 2) + payload


def jpeg_segments(data):
    assert data[:2] == b'\xff\xd8', 'not JPEG'
    at = 2
    while at < len(data):
        assert data[at] == 255, 'invalid JPEG marker'
        while data[at] == 255:
            at += 1
        marker = data[at]; at += 1
        if marker in (0xda, 0xd9):
            return
        if marker == 1 or 0xd0 <= marker <= 0xd7:
            continue
        size = int.from_bytes(data[at:at + 2], 'big')
        assert size >= 2 and at + size <= len(data), 'truncated JPEG segment'
        yield marker, data[at + 2:at + size]
        at += size


def tiff_ifd(data, offset=None):
    assert data[:2] in (b'II', b'MM'), 'not TIFF'
    order = '<' if data[:2] == b'II' else '>'
    def number(at, size):
        assert 0 <= at <= len(data) - size, 'TIFF value outside payload'
        return int.from_bytes(data[at:at + size], 'little' if order == '<' else 'big')
    assert number(2, 2) == 42, 'bad TIFF magic'
    offset = number(4, 4) if offset is None else offset
    count = number(offset, 2)
    assert count <= 4096 and offset + 6 + count * 12 <= len(data), 'bad IFD'
    entries = {}
    sizes = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1, 9: 4, 10: 8, 11: 4, 12: 8}
    for index in range(count):
        at = offset + 2 + index * 12
        tag, kind, length = struct.unpack_from(order + 'HHI', data, at)
        assert tag not in entries, 'duplicate fixture/output tag'
        assert kind in sizes, f'unsupported fixture TIFF type {kind}'
        size = length * sizes[kind]
        start = at + 8 if size <= 4 else number(at + 8, 4)
        assert start <= len(data) and size <= len(data) - start, 'bad TIFF offset'
        raw = data[start:start + size]
        scalar = number(start, sizes[kind]) if kind in (1, 3, 4) and length == 1 else None
        entries[tag] = dict(kind=kind, count=length, raw=raw, scalar=scalar, entry=at)
    return entries, number(offset + 2 + count * 12, 4), offset + 2 + count * 12, order


def thumbnail():
    output = io.BytesIO()
    Image.new('RGB', (12, 8), (15, 220, 90)).save(output, format='JPEG', quality=95)
    data = output.getvalue()
    return data[:2] + jpeg_segment(0xfe, THUMB_TOKEN) + data[2:]


def append_thumbnail(data, *, subifd=False):
    """JPEG/HEIF EXIF IFD1, or TIFF reduced-resolution SubIFD (not a second page)."""
    data = bytearray(data)
    entries, next_ifd, link, order = tiff_ifd(data)
    assert next_ifd == 0
    if subifd:
        assert entries[330]['kind'] == 4 and entries[330]['count'] == 1
        link = entries[330]['entry'] + 8
    at = len(data)
    if at % 2:
        data.append(0); at += 1
    struct.pack_into(order + 'I', data, link, at)
    thumb = thumbnail()
    tags = [(254, 4, 1), (256, 4, 12), (257, 4, 8), (259, 3, 6),
            (513, 4, at + 2 + 6 * 12 + 4), (514, 4, len(thumb))]
    data.extend(struct.pack(order + 'H', len(tags)))
    for tag, kind, value in tags:
        data.extend(struct.pack(order + 'HHI', tag, kind, 1))
        data.extend(struct.pack(order + ('H' if kind == 3 else 'I'), value))
        if kind == 3:
            data.extend(b'\0\0')
    data.extend(b'\0'*4 + thumb)
    return bytes(data)


def assert_fixture_tiff(data, *, subifd=False):
    entries, next_ifd, _, _ = tiff_ifd(data)
    assert entries[700]['raw'] == XMP, 'fixture missing actual TIFF XMP'
    exif, _, _, _ = tiff_ifd(data, entries[34665]['scalar'])
    assert (exif[40962]['scalar'], exif[40963]['scalar']) == STALE_SIZE, 'fixture missing stale dimensions'
    offset = entries[330]['scalar'] if subifd else next_ifd
    assert offset, 'fixture missing real thumbnail directory'
    thumb, _, _, _ = tiff_ifd(data, offset)
    start, size = thumb[513]['scalar'], thumb[514]['scalar']
    payload = data[start:start+size]
    assert THUMB_TOKEN in payload
    with Image.open(io.BytesIO(payload)) as image:
        image.load(); assert image.size == (12, 8), 'not a decodable JPEG thumbnail'


def native_jpeg_fixture(image, tags, path):
    exif = append_thumbnail(tags.tobytes()[6:])
    assert_fixture_tiff(exif)
    output = io.BytesIO(); image.save(output, format='JPEG', quality=95, subsampling=0)
    data = output.getvalue()
    path.write_bytes(data[:2] + jpeg_segment(0xe1, b'Exif\0\0' + exif) +
                     jpeg_segment(0xe1, XMP_JPEG_PREFIX + XMP) + data[2:])
    segments = list(jpeg_segments(path.read_bytes()))
    assert any(marker == 0xe1 and payload == XMP_JPEG_PREFIX + XMP for marker, payload in segments)
    assert_fixture_tiff(next(payload[6:] for marker, payload in segments if marker == 0xe1 and payload.startswith(b'Exif\0\0')))
    return exif


def native_tiff_fixture(image, tags, path):
    values = Image.Exif(); values.load(tags.tobytes()); values[330] = 0
    image.save(path, exif=values.tobytes())
    path.write_bytes(append_thumbnail(path.read_bytes(), subifd=True))
    assert_fixture_tiff(path.read_bytes(), subifd=True)
    with Image.open(path) as decoded:
        decoded.load(); assert decoded.size == image.size and decoded.n_frames == 1


def xmp_png(image, exif, path):
    info = PngImagePlugin.PngInfo()
    info.add_itxt('XML:com.adobe.xmp', XMP.decode('utf-8'))
    image.save(path, exif=b'Exif\0\0' + exif, pnginfo=info)


def boxes(data, start=0, end=None):
    end = len(data) if end is None else end
    while start < end:
        assert start + 8 <= end, 'truncated BMFF box'
        size, kind = struct.unpack_from('>I4s', data, start); header = 8
        if size == 1:
            assert start + 16 <= end
            size = int.from_bytes(data[start+8:start+16], 'big'); header = 16
        if size == 0:
            size = end - start
        assert size >= header and size <= end - start, 'invalid BMFF box size'
        yield kind, start + header, start + size
        start += size


def bmff_metadata(data):
    """Inspect stored item declarations/references, without decoding/rewriting the image."""
    items, references = [], []
    for kind, begin, end in boxes(data):
        if kind != b'meta':
            continue
        for child, a, b in boxes(data, begin + 4, end):
            if child == b'iinf':
                version = data[a]
                count_size = 2 if version == 0 else 4
                count = int.from_bytes(data[a+4:a+4+count_size], 'big')
                entries = list(boxes(data, a + 4 + count_size, b)); assert len(entries) == count
                for entry, x, y in entries:
                    assert entry == b'infe' and data[x] in (2, 3), 'unsupported item-info version'
                    id_size = 2 if data[x] == 2 else 4
                    item_type = data[x+4+id_size+2:x+4+id_size+6]
                    items.append((item_type, data[x+4+id_size+6:y]))
            elif child == b'iref':
                references.extend(kind for kind, _, _ in boxes(data, a + 4, b))
    assert items, 'no BMFF item declarations inspected'
    return items, references


def assert_clean_tiff(data, size, allow_photo):
    entries, next_ifd, _, _ = tiff_ifd(data)
    assert next_ifd == 0 and not ({330, 513, 514, 700, 34853, 270, 274} & entries.keys()), 'private EXIF/thumbnail copied'
    exif = tiff_ifd(data, entries[34665]['scalar'])[0] if 34665 in entries else {}
    assert not ({37500, 42033, 513, 514, 700} & exif.keys()), 'private nested EXIF copied'
    for fields in (entries, exif):
        for tag, expected in zip((40962, 40963), size):
            assert tag not in fields or fields[tag]['scalar'] == expected, 'stale EXIF size copied'
    if not allow_photo:
        assert not ({315, 33432, 40093} & entries.keys()) and 36867 not in exif, 'photo tags copied to stripped output'


def png_chunks(data):
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    at = 8
    while at < len(data):
        size = int.from_bytes(data[at:at+4], 'big'); end = at + size + 12
        assert end <= len(data)
        kind, payload = data[at+4:at+8], data[at+8:at+8+size]
        assert zlib.crc32(kind + payload) == int.from_bytes(data[at+8+size:end], 'big')
        yield kind, payload
        at = end


def riff_chunks(data):
    assert data[:4] == b'RIFF' and data[8:12] == b'WEBP' and int.from_bytes(data[4:8], 'little') + 8 == len(data)
    at = 12
    while at < len(data):
        size = int.from_bytes(data[at+4:at+8], 'little'); end = at + 8 + size
        assert end <= len(data)
        yield data[at:at+4], data[at+8:end]
        at = end + (size & 1)
    assert at == len(data)


def gif_extensions(data):
    assert data[:6] in (b'GIF87a', b'GIF89a')
    at = 13 + (3 * 2**((data[10] & 7)+1) if data[10] & 128 else 0)
    while at < len(data):
        kind = data[at]; at += 1
        if kind == 0x3b:
            return
        if kind == 0x2c:
            flags = data[at+8]; at += 9
            if flags & 128:
                at += 3 * 2**((flags & 7)+1)
            at += 1  # LZW minimum code size
            label = None
        else:
            assert kind == 0x21, 'invalid GIF block'
            label = data[at]; at += 1
        parts = []
        while True:
            size = data[at]; at += 1
            if not size:
                break
            assert at + size <= len(data)
            parts.append(data[at:at+size]); at += size
        if label is not None:
            yield label, parts


def assert_clean(path, size=(40, 24), allow_photo=False):
    data = path.read_bytes(); kind = path.suffix.lower()
    assert XMP_TOKEN not in data and THUMB_TOKEN not in data, f'private payload copied: {path.name}'
    if kind in ('.avif', '.heic'):
        items, refs = bmff_metadata(data)
        assert not any(tag in (b'Exif', b'mime') for tag, _ in items), 'metadata BMFF item copied'
        assert b'thmb' not in refs, 'HEIF thumbnail reference copied'
        return
    with Image.open(path) as image:
        image.load(); assert image.size == size, (path, image.size)
        assert getattr(image, 'n_frames', 1) == 1, 'thumbnail became a second frame'
    if kind in ('.jpg', '.jpeg'):
        for marker, payload in jpeg_segments(data):
            assert not (marker == 0xe1 and payload.startswith(XMP_JPEG_PREFIX)), 'JPEG XMP copied'
            if marker == 0xe1 and payload.startswith(b'Exif\0\0'):
                assert_clean_tiff(payload[6:], size, allow_photo)
    elif kind in ('.tif', '.tiff'):
        assert_clean_tiff(data, size, allow_photo)
    elif kind == '.png':
        assert not ({b'eXIf', b'iTXt', b'tEXt', b'zTXt', b'iCCP'} & {tag for tag, _ in png_chunks(data)}), 'PNG metadata chunk copied'
    elif kind == '.webp':
        chunks = list(riff_chunks(data))
        assert not ({b'EXIF', b'XMP ', b'ICCP'} & {tag for tag, _ in chunks}), 'WebP metadata chunk copied'
        assert all(not (payload[0] & 0x2c) for tag, payload in chunks if tag == b'VP8X'), 'WebP metadata flag set'
    elif kind == '.gif':
        for label, parts in gif_extensions(data):
            assert label != 0xfe, 'GIF comment copied'
            if label == 0xff:
                assert parts[0] in (b'NETSCAPE2.0', b'ANIMEXTS1.0'), 'GIF metadata application copied'
    elif kind == '.bmp':
        assert data[:2] == b'BM'
        header = int.from_bytes(data[14:18], 'little')
        if header >= 124:
            assert not any(data[14+112:14+120]), 'BMP color-profile payload retained'
    else:
        raise AssertionError(f'uninspected output format {kind}')


def contaminated_copy(source, destination):
    """Valid negative controls; hide sentinel strings so native checks must detect the leak."""
    xmp = XMP.replace(XMP_TOKEN, b'ordinary-private-description')
    data = source.read_bytes(); kind = source.suffix.lower()
    if kind == '.jpg':
        data = data[:2] + jpeg_segment(0xe1, XMP_JPEG_PREFIX+xmp) + data[2:]
    elif kind == '.tiff':
        with Image.open(source) as image:
            values = image.getexif(); values[700] = xmp
            image.save(destination, exif=values.tobytes())
        return
    elif kind == '.png':
        payload = b'XML:com.adobe.xmp\0\0' + zlib.compress(xmp)
        chunk = struct.pack('>I', len(payload)) + b'zTXt' + payload + struct.pack('>I', zlib.crc32(b'zTXt'+payload))
        assert data[-8:-4] == b'IEND'
        data = data[:-12] + chunk + data[-12:]
    elif kind == '.webp':
        with Image.open(source) as image:
            image.save(destination, lossless=True, xmp=xmp)
        return
    elif kind == '.gif':
        payload = bytes([11])+b'XMP DataXMP'
        for at in range(0, len(xmp), 255):
            part = xmp[at:at+255]; payload += bytes([len(part)])+part
        assert data[-1:] == b'\x3b'
        data = data[:-1] + b'\x21\xff' + payload + b'\0\x3b'
    elif kind == '.bmp':
        # A real BITMAPV5HEADER with an embedded valid ICC profile. Its offset
        # is relative to the DIB, after the pixel array (not a marker-string test).
        header = int.from_bytes(data[14:18], 'little')
        pixels_at = int.from_bytes(data[10:14], 'little')
        assert header == 40 and pixels_at == 54, 'update BMP negative control for a different encoder layout'
        dib = bytearray(data[14:54]+b'\0'*84)
        pixels = data[pixels_at:]
        profile = ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
        struct.pack_into('<I', dib, 0, 124)
        struct.pack_into('<I', dib, 56, 0x4d424544)  # PROFILE_EMBEDDED
        struct.pack_into('<II', dib, 112, 124+len(pixels), len(profile))
        prefix = bytearray(data[:14]); struct.pack_into('<I', prefix, 2, 14+124+len(pixels)+len(profile))
        struct.pack_into('<I', prefix, 10, 14+124)
        data = bytes(prefix+dib)+pixels+profile
    else:
        raise AssertionError(f'no negative-control writer for {kind}')
    destination.write_bytes(data)
