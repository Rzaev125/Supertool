"""Real HEVC-in-HEIF conversion, metadata refusal and resource-limit regression tests."""
import argparse
import hashlib
import random
import shutil
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageCms

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=Path)
args = parser.parse_args()
exe = args.exe.resolve()
module = exe.parent / 'modules' / 'heic'
test_output = Path.cwd() / 'test-output'
test_output.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='heic-', dir=test_output)) / 'Фото с пробелами'
root.mkdir()
counter = 0


def run(command):
    return subprocess.run([str(x) for x in command], capture_output=True, timeout=180,
                          creationflags=subprocess.CREATE_NO_WINDOW)


def convert(source, target, fail=False, executable=exe, **options):
    global counter
    counter += 1
    output = root / f'Результат {counter}.{target}'
    work = root / f'work-{counter}'
    before = hashlib.sha256(source.read_bytes()).digest()
    snapshot = root / f'options-{counter}.ini'
    snapshot.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k, v in options.items()), encoding='utf-8')
    process = run([executable, '--convert', source, output, work, target, '--options', snapshot])
    error = Path(str(work) + '.error.txt')
    detail = error.read_text('utf-8') if error.exists() else process.stderr.decode(errors='replace')
    assert hashlib.sha256(source.read_bytes()).digest() == before, 'source changed'
    assert not work.exists(), 'staging leaked'
    if fail:
        assert process.returncode != 0 and not output.exists(), f'unsupported image accepted: {source}'
        return detail
    assert process.returncode == 0 and output.exists(), detail or f'exit {process.returncode}'
    return output


def encode(source, name, *extra):
    output = root / name
    result = run([module / 'heif-enc.exe', '-e', 'x265', '-q', '90', '-p', 'preset=fast',
                  '-p', 'x265:pools=1', '-p', 'x265:frame-threads=1',
                  '--colour_primaries', '1', '--transfer_characteristic', '13',
                  '--matrix_coefficients', '6', *extra, '-o', output, source])
    assert result.returncode == 0, result.stderr
    return output


source = root / 'Прозрачное фото.png'
image = Image.new('RGBA', (80, 48), (80, 150, 200, 128))
image.paste((200, 50, 40, 255), (0, 0, 40, 24))
image.paste((20, 180, 50, 0), (40, 24, 80, 48))
image.save(source)
heic = convert(source, 'heic')
decoded = convert(heic, 'png')
with Image.open(decoded) as image:
    assert image.size == (80, 48), 'HEVC padding crop lost'
    rgba = image.convert('RGBA')
    assert abs(rgba.getpixel((50, 10))[3] - 128) <= 2, 'partial alpha lost'
    assert rgba.getpixel((10, 10))[3] == 255 and rgba.getpixel((60, 30))[3] <= 1

# Extension alias, Unicode paths, all supported raster bridges, and resize snapshots.
alias = root / 'Копия.HEIF'; shutil.copy2(heic, alias)
for target in ['png', 'jpg', 'bmp', 'tiff', 'webp', 'avif']:
    output = convert(alias, target, image_max_dimension=40)
    inspect = convert(output, 'png') if target in ['avif', 'webp'] else output
    assert Image.open(inspect).size == (40, 24), target
for target in ['webp', 'avif']:
    modern = convert(source, target)
    back = convert(convert(modern, 'heic'), 'png')
    assert Image.open(back).size == (80, 48)

# HEIF transforms are applied, including non-square quarter-turns and codec crop.
rotated = encode(source, 'rotated.heic', '--rotate-cw', '90')
turned = Image.open(convert(rotated, 'png')).convert('RGBA')
assert turned.size == (48, 80), turned.size
assert turned.getpixel((36, 20))[0] > 150 and turned.getpixel((36, 20))[1] < 100
mirrored = encode(source, 'mirrored.heic', '--flip-h')
flipped = Image.open(convert(mirrored, 'png')).convert('RGBA')
assert flipped.getpixel((60, 12))[0] > 150 and flipped.getpixel((60, 12))[1] < 100

# An embedded ICC profile must reach WIC color management, then be normalized.
profile = ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
profile_source = root / 'profile.png'
Image.open(source).save(profile_source, icc_profile=profile)
profile_heic = encode(profile_source, 'profile.heic')
profile_result = Image.open(convert(profile_heic, 'png')).convert('RGBA')
assert abs(profile_result.getpixel((50, 10))[1] - 150) <= 8, 'ICC path changed colors'

multi = encode(source, 'multi.heic', source)
assert 'многокадров' in convert(multi, 'png', fail=True).lower()
hdr = encode(source, 'hdr.heic', '--transfer_characteristic', '16')
assert 'HDR' in convert(hdr, 'png', fail=True)
light = encode(source, 'light-level.heic', '--clli', '1000,400')
assert 'HDR' in convert(light, 'png', fail=True)
sequence = encode(source, 'sequence.heic', '-S', source)
assert 'многокадров' in convert(sequence, 'png', fail=True).lower()
broken = root / 'broken.heic'; broken.write_bytes(b'not a HEIC')
convert(broken, 'png', fail=True)

# Mutate a real ispe box: validation must reject giant dimensions before decoding.
oversized = root / 'oversized.heic'
data = bytearray(heic.read_bytes()); offset = data.index(b'ispe')
data[offset + 8:offset + 12] = (32769).to_bytes(4, 'big'); oversized.write_bytes(data)
convert(oversized, 'png', fail=True)

noise = root / 'noise.png'
Image.frombytes('RGB', (256, 128), random.Random(8).randbytes(256 * 128 * 3)).save(noise)
compact = convert(noise, 'heic', quality='compact')
high = convert(noise, 'heic', quality='high')
assert high.stat().st_size > compact.stat().st_size * 1.3, 'HEIC quality ignored'
isolated = root / 'missing-module'; isolated.mkdir(); shutil.copy2(exe, isolated / exe.name)
assert 'Модуль' in convert(source, 'heic', fail=True, executable=isolated / exe.name)
print('PASS HEIC: encode/decode, alpha, Unicode, all bridges, resize, transforms, ICC, limits, HDR/multiframe refusal, presets, missing module')
