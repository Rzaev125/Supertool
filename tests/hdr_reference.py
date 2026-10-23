"""Independent double-precision HDR and PNG16 fixture/reference utilities."""
import math
import struct
import zlib
from PIL import Image


def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


def write_png16(path, width, height, pixels):
    raw = b''.join(b'\0' + struct.pack('>' + 'H' * width * 4,
                                     *(v for p in pixels[y * width:(y + 1) * width] for v in p))
                   for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 16, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def read_png16(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, compressed = 8, bytearray()
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        kind, payload = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + size]
        assert zlib.crc32(kind + payload) == struct.unpack_from('>I', data, offset + 8 + size)[0]
        if kind == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            assert depth == 16 and color in [2, 6] and not interlace, 'decoder lost 16-bit RGB precision'
        elif kind == b'IDAT':
            compressed.extend(payload)
        offset += size + 12
    channels = 4 if color == 6 else 3
    bpp, stride = channels * 2, width * channels * 2
    raw, previous, pixels = zlib.decompress(compressed), bytearray(stride), []
    assert len(raw) == height * (stride + 1)
    for y in range(height):
        mode = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left, up, corner = (row[x - bpp] if x >= bpp else 0), previous[x], (previous[x - bpp] if x >= bpp else 0)
            if mode == 1:
                predictor = left
            elif mode == 2:
                predictor = up
            elif mode == 3:
                predictor = (left + up) // 2
            elif mode == 4:
                estimate = left + up - corner
                distances = [abs(estimate - left), abs(estimate - up), abs(estimate - corner)]
                predictor = [left, up, corner][distances.index(min(distances))]
            else:
                assert mode == 0
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        words = struct.unpack('>' + 'H' * width * channels, row)
        pixels.extend(tuple(words[x:x + channels]) + (() if channels == 4 else (65535,))
                      for x in range(0, len(words), channels))
        previous = row
    return width, height, pixels


def pq_code(nits):
    value = (nits / 10000) ** (2610 / 16384)
    return ((3424 / 4096 + 2413 / 128 * value) / (1 + 2392 / 128 * value)) ** (2523 / 32)


def linear709(pixel, transfer):
    values = [value / 65535 for value in pixel[:3]]
    if transfer == 'pq':
        def decode(value):
            power = value ** (32 / 2523)
            return 10000 * (max(power - 3424 / 4096, 0) / (2413 / 128 - 2392 / 128 * power)) ** (16384 / 2610)
        rgb = list(map(decode, values))
    else:
        assert transfer == 'hlg'
        a = .17883277
        b = 1 - 4 * a
        c = .5 - a * math.log(4 * a)
        scene = [value * value / 3 if value <= .5 else (math.exp((value - c) / a) + b) / 12 for value in values]
        luminance = sum(weight * value for weight, value in zip([.2627, .6780, .0593], scene))
        rgb = [1000 * value * luminance ** .2 for value in scene]
    matrix = [(1.660491002108434, -.58764113878855, -.072849863319884),
              (-.124550474521591, 1.13289989712596, -.008349422604369),
              (-.018150763354905, -.100578898008007, 1.118729661362912)]
    return [max(0, sum(a * b for a, b in zip(row, rgb))) / 100 for row in matrix]


def hable(x):
    return (x * (.15 * x + .05) + .004) / (x * (.15 * x + .5) + .06) - 1 / 15


def tone_reference(width, height, pixels, transfer):
    linear = [linear709(pixel, transfer) for pixel in pixels]
    peak = max([1] + [max(rgb) for rgb, p in zip(linear, pixels) if p[3]])
    normalization = hable(peak)
    result = Image.new('RGBA', (width, height))
    for index, (rgb, pixel) in enumerate(zip(linear, pixels)):
        signal = max(rgb)
        scale = hable(signal) / normalization / signal if signal else 0
        encoded = []
        for value in rgb:
            value = max(0, min(1, value * scale))
            value = 12.92 * value if value <= .0031308 else 1.055 * value ** (1 / 2.4) - .055
            encoded.append(round(value * 255))
        result.putpixel((index % width, index // width), (*encoded, round(pixel[3] / 257)))
    return result


def chart(transfer, width=80, height=48):
    if transfer == 'pq':
        values = [tuple([pq_code(v)] * 3) for v in [0, .1, 1, 10, 100, 203, 1000, 10000]]
        values += [tuple(map(pq_code, rgb)) for rgb in [(400, 30, 10), (30, 400, 10), (30, 10, 400), (200, 200, 200)]]
    else:
        values = [tuple([v] * 3) for v in [0, .02, .1, .25, .5, .65, .8, 1]]
        values += [(.8, .3, .1), (.3, .8, .1), (.3, .1, .8), (.65, .65, .65)]
    pixels = []
    for y in range(height):
        for x in range(width):
            rgb = values[min(len(values) - 1, x * len(values) // width)]
            alpha = [65535, 32768, 16384, 0][y // (height // 4)]
            pixels.append(tuple(round(v * 65535) for v in rgb) + (alpha,))
    return width, height, pixels
