"""Container display transforms checked against independent Pillow pixel operations."""
import argparse
import hashlib
import math
import struct
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageChops, ImageCms

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=Path)
exe = parser.parse_args().exe.resolve()
parent = Path.cwd() / 'test-output'
parent.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='avif-transforms-', dir=parent))
encoder = exe.parent / 'modules/avif/avifenc.exe'
counter = 0


def run(command):
    return subprocess.run([str(x) for x in command], capture_output=True, timeout=120,
                          creationflags=subprocess.CREATE_NO_WINDOW)


def convert(source, target='png', fail=False, **options):
    global counter
    counter += 1
    output, work = root / f'result-{counter}.{target}', root / f'work-{counter}'
    ini = root / f'options-{counter}.ini'
    ini.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k, v in options.items()), encoding='utf-8')
    original = hashlib.sha256(source.read_bytes()).digest()
    process = run([exe, '--convert', source, output, work, target, '--options', ini])
    error = Path(str(work) + '.error.txt')
    detail = error.read_text(encoding='utf-8') if error.exists() else str(process.stderr)
    assert hashlib.sha256(source.read_bytes()).digest() == original, 'source changed'
    assert not work.exists(), 'staging leaked'
    if fail:
        assert process.returncode != 0 and not output.exists(), f'unsupported AVIF accepted: {source}'
        return detail
    assert process.returncode == 0 and output.is_file(), f'{source.name}: {detail}'
    return output


def encode(name, *options, png=None):
    out = root / (name + '.avif')
    process = run([encoder, '-j', '1', '-s', '8', '--qcolor', '100', '--qalpha', '100',
                   '--yuv', '444', '--cicp', '1/13/6', *options, png or source, out])
    assert process.returncode == 0, process.stderr
    return out


def pixels(path):
    with Image.open(path) as image:
        assert image.getexif().get(274, 1) == 1, 'stale orientation'
        return image.convert('RGBA')


def compare(path, expected, tolerance=0):
    actual = pixels(path)
    assert actual.size == expected.size, (path, actual.size, expected.size)
    difference = ImageChops.difference(actual, expected).getextrema()
    assert max(v[1] for v in difference) <= tolerance, (path, difference)


image = Image.new('RGBA', (96, 64))
for y in range(image.height):
    for x in range(image.width):
        image.putpixel((x, y), ((x * 3 + y) % 256, (y * 5 + x) % 256,
                               (x * 2 + y * 3) % 256, [0, 64, 128, 255][x // 24]))
source = root / 'Фото с пробелами.png'
image.save(source)
plain = encode('plain')
baseline = pixels(convert(plain))
rotations = {1: Image.Transpose.ROTATE_90, 2: Image.Transpose.ROTATE_180, 3: Image.Transpose.ROTATE_270}
mirrors = {0: Image.Transpose.FLIP_TOP_BOTTOM, 1: Image.Transpose.FLIP_LEFT_RIGHT}

# Encoder property metadata changes the display, not the encoded pixel planes.
for angle in range(4):
    for axis in [None, 0, 1]:
        flags = ['--irot', str(angle)]
        expected = baseline.transpose(rotations[angle]) if angle else baseline.copy()
        if axis is not None:
            flags += ['--imir', str(axis)]
            expected = expected.transpose(mirrors[axis])
        avif = encode(f'rotate-{angle}-mirror-{axis}', *flags)
        compare(convert(avif), expected)

for angle, axis in [(0, None), (1, 0), (2, 1), (3, 0)]:
    flags = ['--crop', '8,6,64,40', '--irot', str(angle)]
    expected = baseline.crop((8, 6, 72, 46))
    if angle:
        expected = expected.transpose(rotations[angle])
    if axis is not None:
        flags += ['--imir', str(axis)]
        expected = expected.transpose(mirrors[axis])
    avif = encode(f'crop-{angle}-{axis}', *flags)
    compare(convert(avif), expected)

rotated = encode('bridges', '--irot', '1', '--imir', '1')
expected = baseline.transpose(rotations[1]).transpose(mirrors[1])
for target in ['png', 'tiff', 'webp']:
    result = convert(rotated, target)
    if target == 'webp':
        actual = pixels(convert(result))
        assert actual.size == expected.size
        assert actual.getchannel('A').tobytes() == expected.getchannel('A').tobytes()
        # libwebp may discard invisible RGB under fully transparent pixels.
        for y in range(expected.height):
            for x in range(expected.width):
                if expected.getpixel((x, y))[3]:
                    assert actual.getpixel((x, y)) == expected.getpixel((x, y))
    else:
        compare(result, expected)
for target in ['jpg', 'bmp']:
    result = pixels(convert(rotated, target))
    assert result.size == expected.size
    # Fully transparent strip moves with the image, and must blend to white.
    for x, y in [(4, 90), (20, 80)]:
        assert expected.getpixel((x, y))[3] == 0
        assert min(result.getpixel((x, y))[:3]) >= 250
    if target == 'bmp':
        white = Image.new('RGBA', expected.size, 'white')
        compare(root / f'result-{counter}.bmp', Image.alpha_composite(white, expected))
for target in ['avif', 'heic', 'gif']:
    normalized = pixels(convert(convert(rotated, target)))
    assert normalized.size == expected.size, (target, normalized.size)
    # Lossy/palette bridges: compare opaque landmarks to catch second transforms.
    for x, y in [(5, 5), (50, 10)]:
        a, b = normalized.getpixel((x, y)), expected.getpixel((x, y))
        assert b[3] == 255 and a[3] == 255
        assert max(abs(a[i] - b[i]) for i in range(3)) <= 45, (target, a, b)
    opaque_error = []
    for y in range(expected.height):
        for x in range(expected.width):
            b = expected.getpixel((x, y))
            if b[3] == 255:
                a = normalized.getpixel((x, y))
                opaque_error.extend(abs(a[i] - b[i]) for i in range(3))
    assert sum(opaque_error) / len(opaque_error) < 15, target
def area_reference(image, size):
    """Independent area integration in premultiplied RGBA, not WIC/FFmpeg."""
    out = Image.new('RGBA', size)
    sx, sy = image.width / size[0], image.height / size[1]
    for y in range(size[1]):
        for x in range(size[0]):
            sums = [0.0] * 4
            for iy in range(math.floor(y * sy), math.ceil((y + 1) * sy)):
                wy = min((y + 1) * sy, iy + 1) - max(y * sy, iy)
                for ix in range(math.floor(x * sx), math.ceil((x + 1) * sx)):
                    weight = wy * (min((x + 1) * sx, ix + 1) - max(x * sx, ix))
                    pixel = image.getpixel((min(ix, image.width - 1), min(iy, image.height - 1)))
                    for channel in range(3):
                        sums[channel] += pixel[channel] * pixel[3] / 255 * weight
                    sums[3] += pixel[3] * weight
            alpha = sums[3] / (sx * sy)
            rgb = [round(value * 255 / sums[3]) if sums[3] else 0 for value in sums[:3]]
            out.putpixel((x, y), (*rgb, round(alpha)))
    return out


def check_resize(actual, reference):
    assert actual.size == reference.size
    alpha_error = ImageChops.difference(actual.getchannel('A'), reference.getchannel('A'))
    assert alpha_error.getextrema()[1] <= 1, 'resized alpha content changed'
    errors = []
    for y in range(reference.height):
        for x in range(reference.width):
            a, b = actual.getpixel((x, y)), reference.getpixel((x, y))
            if b[3]:
                errors.extend(abs(a[i] - b[i]) for i in range(3))
    # Fant and exact area integration differ around discontinuities in this
    # wrapped color chart. Bound every error, mean and 95th percentile together.
    errors.sort()
    assert errors[-1] <= 24 and sum(errors) / len(errors) <= 2, 'resized RGB content changed'
    assert errors[int(len(errors) * .95)] <= 4, 'resized RGB regions changed'
    for x, y in [(3, 3), (15, 6), (4, 10), (17, 20)]:
        a, b = actual.getpixel((x, y)), reference.getpixel((x, y))
        assert b[3] and max(abs(a[i] - b[i]) for i in range(3)) <= 5, 'resized landmark changed'


small = pixels(convert(rotated, image_max_dimension='32'))
reference = area_reference(expected, (21, 32))
check_resize(small, reference)
# Same dimensions alone would accept these wrong images. Prove the new check
# detects reversed content, reversed transparency and an opaque replacement.
opaque = small.copy()
opaque.putalpha(255)
for damaged in [small.transpose(Image.Transpose.FLIP_LEFT_RIGHT),
                small.transpose(Image.Transpose.FLIP_TOP_BOTTOM), opaque]:
    try:
        check_resize(damaged, reference)
    except AssertionError:
        pass
    else:
        raise AssertionError('resize checker accepted a same-size corrupted image')

# ICC conversion is pointwise and must commute with a display transform.
profile = bytearray(ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes())
for index in range(struct.unpack_from('>I', profile, 128)[0]):
    record = 132 + index * 12
    if profile[record:record + 4] in [b'rTRC', b'gTRC', b'bTRC']:
        offset = struct.unpack_from('>I', profile, record + 4)[0]
        struct.pack_into('>I', profile, record + 8, 16)
        profile[offset:offset + 16] = b'para' + bytes(8) + struct.pack('>i', 65536)
profile[84:100] = bytes(16)
profiled = root / 'linear-profile.png'
image.save(profiled, icc_profile=bytes(profile))
managed = pixels(convert(encode('profile-plain', png=profiled)))
compare(convert(encode('profile-rotated', '--irot', '3', '--imir', '0', png=profiled)),
        managed.transpose(rotations[3]).transpose(mirrors[0]))

# Container rotation is authoritative. The decoder resets retained historical
# EXIF orientation before PNG output; no second EXIF rotation may be applied.
exif = Image.Exif()
exif[274] = 6
payload = exif.tobytes()
exif_file = root / 'orientation.exif'
exif_file.write_bytes(payload)
historical = encode('historical-exif', '--exif', exif_file, '--irot', '1')
assert payload[6:] in historical.read_bytes(), 'fixture lacks EXIF orientation'
result = convert(historical)
compare(result, baseline.transpose(rotations[1]))
with Image.open(result) as decoded:
    assert not decoded.getexif(), 'historical EXIF leaked'

# PASP has no pixel implementation in the PNG decoder path. Keep refusing it.
convert(encode('aspect', '--pasp', '2,1'), fail=True)
convert(encode('hdr-transform', '--irot', '1', '--cicp', '9/16/9'), fail=True)

# Corrupt an actual property while retaining the BMFF box layout and offsets.
def broken_property(path, tag, offset, value, name):
    data = bytearray(path.read_bytes())
    marker = data.index(tag)
    size = struct.unpack_from('>I', data, marker - 4)[0]
    assert 8 <= size <= 64 and offset < size - 8
    data[marker + 4 + offset] = value
    result = root / (name + '.avif')
    result.write_bytes(data)
    return result


convert(broken_property(rotated, b'irot', 0, 255, 'bad-rotation'), fail=True)
crop = encode('crop-valid', '--crop', '8,6,64,40')
convert(broken_property(crop, b'clap', 7, 0, 'bad-crop-denominator'), fail=True)
# avifdec 1.4.2 reports an ignored transform for a full-frame aperture, then
# skips subsequent rotation too. Refuse that fallback rather than publish it.
convert(encode('ignored-full-crop', '--crop', '0,0,96,64', '--irot', '1'), fail=True)
print('PASS AVIF display transforms: rotations, mirror axes, crop order, alpha, bridges, resize, refusals and source preservation')
