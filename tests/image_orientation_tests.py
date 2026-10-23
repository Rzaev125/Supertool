"""Real EXIF orientation fixtures with independent pixel transforms and source hashes."""
import argparse
import hashlib
import struct
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageChops

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
exe = parser.parse_args().exe.resolve()
root_parent = Path.cwd() / 'test-output'
root_parent.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='orientation-', dir=root_parent))
number = 0

def convert(source, target='png', fail=False, **options):
    global number
    number += 1
    out, work = root / f'output-{number}.{target}', root / f'work-{number}'
    ini = root / f'options-{number}.ini'
    ini.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k,v in options.items()), encoding='utf-8')
    before = hashlib.sha256(source.read_bytes()).digest()
    p = subprocess.run([str(x) for x in [exe, '--convert', source, out, work, target, '--options', ini]],
                       capture_output=True, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
    error = Path(str(work) + '.error.txt')
    detail = error.read_text('utf-8') if error.exists() else str(p.stderr)
    assert before == hashlib.sha256(source.read_bytes()).digest(), 'source modified'
    assert not work.exists(), 'staging leaked'
    if fail:
        assert p.returncode != 0 and not out.exists(), f'malformed EXIF accepted: {source}'
        return detail
    assert p.returncode == 0 and out.is_file(), f'{source.name}: {detail}'
    return out

# Asymmetric, non-square pattern: every pixel checked, not only a corner/dimensions.
base = Image.new('RGB', (96, 64))
for y in range(64):
    for x in range(96):
        base.putpixel((x,y), ((x*3+y)%256, (y*5+x)%256, (x*2+y*3)%256))
operations = {2: Image.Transpose.FLIP_LEFT_RIGHT, 3: Image.Transpose.ROTATE_180,
              4: Image.Transpose.FLIP_TOP_BOTTOM, 5: Image.Transpose.TRANSPOSE,
              6: Image.Transpose.ROTATE_270, 7: Image.Transpose.TRANSVERSE,
              8: Image.Transpose.ROTATE_90}

def transformed(image, orientation):
    return image.transpose(operations[orientation]) if orientation != 1 else image.copy()

def compare(actual, expected, tolerance=0):
    with Image.open(actual) as image:
        assert image.size == expected.size, (actual, image.size, expected.size)
        delta = ImageChops.difference(image.convert(expected.mode), expected)
        assert max(v[1] for v in delta.getextrema()) <= tolerance, (actual, delta.getextrema())
        assert image.getexif().get(274, 1) == 1, 'stale orientation copied'

for kind in ['jpg', 'tiff', 'webp']:
    for orientation in range(1,9):
        source = root / f'photo-{kind}-{orientation}.{kind}'
        exif = Image.Exif(); exif[274] = orientation
        base.save(source, exif=exif, quality=98, subsampling=0, lossless=True, dpi=(120,240))
        if kind == 'jpg':
            with Image.open(source) as image: raw = image.convert('RGB')
        else: raw = base
        expected = transformed(raw, orientation)
        out = convert(source)
        compare(out, expected, 3 if kind == 'jpg' else 0)
        if kind == 'tiff':
            with Image.open(out) as image:
                dpi = image.info.get('dpi')
                want = (240,120) if orientation >= 5 else (120,240)
                assert dpi and all(abs(a-b)<1 for a,b in zip(dpi,want)), (dpi,want)
        resized = convert(source, image_max_dimension=48)
        with Image.open(resized) as image:
            assert image.size == ((32,48) if orientation >= 5 else (48,32)), image.size
print('PASS all 8 JPEG/TIFF/WebP orientations, full pixels, DPI and resize', flush=True)

# Alpha remains attached to the same pixels through rotation and reflection.
alpha = base.convert('RGBA'); alpha.putalpha(Image.linear_gradient('L').resize(base.size))
for orientation in [2,5,6,7,8]:
    source = root / f'alpha-{orientation}.webp'; exif=Image.Exif();exif[274]=orientation
    alpha.save(source, lossless=True, exact=True, exif=exif)
    compare(convert(source), transformed(alpha,orientation))

# Modern output bridges normalize once before encoding, including the GIF path.
source = root / 'photo-tiff-6.tiff'
for target in ['jpg','bmp','tiff','webp','avif','heic','gif']:
    out = convert(source,target)
    decoded = convert(out) if target in ['webp','avif','heic','gif'] else out
    with Image.open(decoded) as image: assert image.size == (64,96), target
    if target in ['bmp','tiff','webp']: compare(decoded,transformed(base,6))

for kind in ['jpg','tiff','webp']:
    for bad in [0,9]:
        exif=Image.Exif();exif[274]=bad
        source=root/f'invalid-{kind}-{bad}.{kind}';base.save(source,exif=exif,lossless=True)
        convert(source,fail=True)

# Duplicate EXIF chunks have ambiguous precedence, even if values agree.
source = root / 'photo-webp-6.webp'; data=source.read_bytes(); pos=data.index(b'EXIF')
length=struct.unpack_from('<I',data,pos+4)[0]
extra=data[pos:pos+8+length+(length&1)]
duplicate=bytearray(data+extra);struct.pack_into('<I',duplicate,4,len(duplicate)-8)
bad=root/'duplicate.webp';bad.write_bytes(duplicate);convert(bad,fail=True)
print('PASS alpha, modern bridges, invalid and duplicate EXIF, no overwrite/source changes', flush=True)
