"""Real high-bit-depth PQ/HLG AVIF/HEIC → SDR with independent pixel math."""
import argparse
import hashlib
import shutil
import subprocess
import tempfile
import zlib
from pathlib import Path
from PIL import Image, ImageChops, ImageCms
from hdr_reference import chart, chunk, read_png16, tone_reference, write_png16

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--harness', type=Path)
args = parser.parse_args()
exe = args.exe.resolve()
parent = Path.cwd() / 'test-output'
parent.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='hdr-images-', dir=parent))
modules = exe.parent / 'modules'
counter = 0


def run(command):
    return subprocess.run([str(x) for x in command], capture_output=True, timeout=150,
                          creationflags=subprocess.CREATE_NO_WINDOW)


def convert(source, target='png', fail=False, executable=exe, **options):
    global counter
    counter += 1
    output, work = root / f'result-{counter}.{target}', root / f'work-{counter}'
    ini = root / f'options-{counter}.ini'
    ini.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k, v in options.items()), encoding='utf-8')
    before = hashlib.sha256(source.read_bytes()).digest()
    process = run([executable, '--convert', source, output, work, target, '--options', ini])
    error = Path(str(work) + '.error.txt')
    detail = error.read_text(encoding='utf-8') if error.exists() else str(process.stderr)
    assert before == hashlib.sha256(source.read_bytes()).digest(), 'HDR source changed'
    assert not work.exists(), 'HDR staging leaked'
    if fail:
        assert process.returncode and not output.exists(), f'unsupported HDR accepted: {source}'
        return detail
    assert process.returncode == 0 and output.exists(), f'{source.name}: {detail}'
    return output


def encode(source, kind, transfer, depth, name, *extra):
    target = root / (name + '.' + kind)
    trc = '16' if transfer == 'pq' else '18'
    if kind == 'avif':
        command = [modules / 'avif/avifenc.exe', '-j', '1', '-s', '8', '--qcolor', '100', '--qalpha', '100',
                   '--depth', depth, '--yuv', '444', '--cicp', f'9/{trc}/9', *extra, source, target]
    else:
        command = [modules / 'heic/heif-enc.exe', '-e', 'x265', '-q', '100', *(['-b', depth] if depth > 8 else []),
                   '-p', 'preset=fast', '-p', 'chroma=444', '-p', 'x265:pools=1', '-p', 'x265:frame-threads=1',
                   '--colour_primaries', '9', '--transfer_characteristic', trc, '--matrix_coefficients', '9',
                   *extra, '-o', target, source]
    process = run(command)
    assert process.returncode == 0, process.stderr
    return target


def decoded16(source):
    out = root / (source.name + '-raw16.png')
    if source.suffix == '.avif':
        command = [modules / 'avif/avifdec.exe', '-j', '1', '--depth', '16', source, out]
    else:
        command = [modules / 'heic/heif-dec.exe', '--quiet', '--codec-threads', '1', '--tile-threads', '1', source, out]
    process = run(command)
    assert process.returncode == 0, process.stderr
    return out, read_png16(out)


def check(output, expected, tolerance=2):
    with Image.open(output) as image:
        actual = image.convert('RGBA')
        assert actual.size == expected.size
        difference = ImageChops.difference(actual, expected).getextrema()
        assert max(v[1] for v in difference) <= tolerance, (output, difference)
        assert not image.info.get('icc_profile'), 'HDR ICC profile leaked'
        assert not ({274, 34665, 34853, 37500} & set(image.getexif())), 'HDR EXIF leaked'
        if output.suffix == '.png':
            assert output.read_bytes()[24] == 8


fixtures = []
for transfer in ['pq', 'hlg']:
    width, height, codes = chart(transfer)
    source = root / (transfer + '-chart.png')
    write_png16(source, width, height, codes)
    for kind in ['avif', 'heic']:
        for depth in [10, 12]:
            encoded = encode(source, kind, transfer, depth, f'{transfer}-{kind}-{depth}')
            raw_path, (w, h, decoded) = decoded16(encoded)
            assert (w, h) == (width, height)
            # Detect range truncation, decoder tone mapping or premature8-bit decode.
            for x, y in [(35, 4), (55, 4), (65, 4)]:
                a, b = decoded[y * w + x], codes[y * w + x]
                assert max(abs(a[i] - b[i]) for i in range(3)) <= 256, ('unexpected decoder precision/range', kind, depth, a, b)
            expected = tone_reference(w, h, decoded, transfer)
            output = convert(encoded)
            check(output, expected)
            wrong_transfer = tone_reference(w, h, decoded, 'hlg' if transfer == 'pq' else 'pq')
            try:
                check(output, wrong_transfer)
            except AssertionError:
                pass
            else:
                raise AssertionError('pixel checker accepted the wrong HDR transfer function')
            with Image.open(output) as image:
                rgb = image.convert('RGB')
                assert max(abs(rgb.getpixel((x, 4))[c] - round(decoded[4 * w + x][c] / 257))
                           for x in range(w) for c in range(3)) > 8, 'HDR merely quantized to8-bit'
            fixtures.append((encoded, expected, raw_path, transfer))

# Geometry in the codec container must not become a second pixel transform.
source = root / 'pq-chart.png'
rotated = encode(source, 'avif', 'pq', 12, 'crop-rotate', '--crop', '8,4,64,32', '--irot', '1', '--imir', '0')
_, (w, h, values) = decoded16(rotated)
expected = tone_reference(w, h, values, 'pq')
check(convert(rotated), expected)
small = convert(rotated, image_max_dimension='24')
with Image.open(small) as image:
    assert image.size == (12, 24)
for target in ['tiff', 'webp', 'avif', 'heic', 'gif', 'bmp', 'jpg']:
    result = convert(fixtures[0][0], target)
    if target == 'tiff':
        check(result, fixtures[0][1])
    else:
        with Image.open(convert(result) if target in ['webp', 'avif', 'heic', 'gif'] else result) as decoded:
            assert decoded.size == fixtures[0][1].size
            actual = decoded.convert('RGBA')
            if target in ['jpg', 'bmp']:
                assert min(actual.getpixel((20, 44))[:3]) > 245
            # JPEG chroma subsampling blends across our narrow saturated patches;
            # compare neutral interiors there, retain color checks on other bridges.
            points = [(16, 4), (36, 4), (49, 4)] if target == 'jpg' else [(34, 4), (48, 4), (63, 4)]
            for point in points:
                a, b = actual.getpixel(point), fixtures[0][1].getpixel(point)
                assert max(abs(a[i] - b[i]) for i in range(3)) <= 25, (target, a, b)

# HDR import must work without the optional video module.
isolated = root / 'only-photo-modules'
isolated.mkdir()
shutil.copy2(exe, isolated / exe.name)
for kind in ['avif', 'heic']:
    shutil.copytree(modules / kind, isolated / 'modules' / kind)
for source, expected, _, _ in [fixtures[0], fixtures[2]]:
    check(convert(source, executable=isolated / exe.name), expected)

if args.harness:
    # Direct helper color and ownership tests independent of codec registration.
    for _, expected, raw, transfer in fixtures:
        out = root / (raw.stem + '-helper.png')
        process = run([args.harness.resolve(), raw, out, transfer])
        assert process.returncode == 0, process.stderr
        check(out, expected)
        original = out.read_bytes()
        assert run([args.harness.resolve(), raw, out, transfer]).returncode != 0
        assert out.read_bytes() == original, 'helper overwrote an existing output'

    # Native helper rejects low precision, unknown transfer and self-overwrite.
    low = root / '8bit.png'
    Image.new('RGBA', (8, 8), 'red').save(low)
    for source, transfer in [(low, 'pq'), (fixtures[0][2], 'unknown')]:
        out = root / (source.stem + '-rejected.png')
        assert run([args.harness.resolve(), source, out, transfer]).returncode != 0 and not out.exists()
    raw = fixtures[0][2]
    before = raw.read_bytes()
    assert run([args.harness.resolve(), raw, raw, 'pq']).returncode != 0 and raw.read_bytes() == before

# Unsupported HDR variants must fail without publishing a misleading SDR result.
for kind in ['avif', 'heic']:
    source = root / 'pq-chart.png'
    if kind == 'avif':
        wrong = ['--cicp', '1/16/9']
    else:
        wrong = ['--colour_primaries', '1']
    unsupported = encode(source, kind, 'pq', 10, 'unsupported-' + kind, *wrong)
    assert 'HDR' in convert(unsupported, fail=True)
    if kind == 'heic':
        source = root / 'low-input.png'
        Image.new('RGB', (16, 16), 'gray').save(source)
    low = encode(source, kind, 'pq', 8, 'low-depth-' + kind)
    assert 'HDR' in convert(low, fail=True)
    # Conflicting ICC must never suppress the explicit HDR transfer function.
    source = root / 'pq-chart.png'
    data = source.read_bytes()
    profile = ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
    icc_source = root / ('conflict-' + kind + '.png')
    icc_source.write_bytes(data[:33] + chunk(b'iCCP', b'sRGB\0\0' + zlib.compress(profile)) + data[33:])
    conflict = encode(icc_source, kind, 'pq', 10, 'conflict-' + kind)
    assert 'HDR' in convert(conflict, fail=True)

# RGB matrix0 and ordinary HDR luminance metadata are also supported.
for kind in ['avif', 'heic']:
    extra = ['--cicp', '9/16/0'] if kind == 'avif' else ['-L', '--matrix_coefficients', '0', '--clli', '1000,400']
    encoded = encode(root / 'pq-chart.png', kind, 'pq', 12, 'rgb-' + kind, *extra)
    _, (w, h, decoded) = decoded16(encoded)
    check(convert(encoded), tone_reference(w, h, decoded, 'pq'))

empty = root / 'missing-photo-modules'
empty.mkdir()
shutil.copy2(exe, empty / exe.name)
for source, _, _, _ in [fixtures[0], fixtures[2]]:
    assert convert(source, fail=True, executable=empty / exe.name)

# Fully transparent extreme highlights must not change the exposure of visible pixels.
for transfer in ['pq', 'hlg']:
    width, height, pixels = chart(transfer)
    pixels = [((65535, 65535, 65535, 0) if not p[3] else tuple(min(v, 32000) for v in p[:3]) + (p[3],)) for p in pixels]
    raw = root / (transfer + '-hidden-peak.png')
    write_png16(raw, width, height, pixels)
    if args.harness:
        out = root / (transfer + '-hidden-result.png')
        assert run([args.harness.resolve(), raw, out, transfer]).returncode == 0
        check(out, tone_reference(width, height, pixels, transfer))

print('PASS HDR to SDR: PQ/HLG10/12-bit AVIF/HEIC, independent pixels, alpha, precision, geometry, bridges, guards and ownership')
