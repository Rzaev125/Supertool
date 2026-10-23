"""Dense, lossless RGB code ramps distinguish high precision from padded8-bit PNG."""
import argparse
import subprocess
import tempfile
from pathlib import Path
from hdr_reference import read_png16, write_png16


def check_precision(actual, expected):
    assert len(actual) == len(expected), 'precision sample count changed'
    error = max(abs(a[c] - b[c]) for a, b in zip(actual, expected) for c in range(3))
    # The fixture specifies the pinned codec's expansion independently.
    # Two codes allow rounding differences without admitting
    # the up-to128-code error of even nearest8-bit roundtrip.
    assert error <= 2, f'precision lost: maximum16-bit code error={error}'


def run(command):
    process = subprocess.run([str(x) for x in command], capture_output=True, timeout=120,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    assert process.returncode == 0, process.stderr


parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
args = parser.parse_args()
modules = args.exe.resolve().parent / 'modules'
parent = Path.cwd() / 'test-output'
parent.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='hdr-precision-', dir=parent))

for depth in [10, 12]:
    width, height = 1 << depth, 16
    # Each native code is known before encoding. Lossless identity-matrix RGB
    # avoids YCbCr conversion, chroma subsampling and lossy codec error.
    levels = [(code * 65535 + (width - 1) // 2) // (width - 1) for code in range(width)]
    expected = [(v, v, v, 65535) for y in range(height) for v in (levels if y % 2 == 0 else levels[::-1])]
    source = root / f'ramp{depth}.png'
    write_png16(source, width, height, expected)
    for transfer in [16, 18]:
        for kind in ['avif', 'heic']:
            encoded = root / f'ramp{depth}-{transfer}.{kind}'
            decoded = root / f'ramp{depth}-{transfer}-{kind}.png'
            if kind == 'avif':
                run([modules / 'avif/avifenc.exe', '-j', '1', '-s', '8', '--lossless', '--depth', depth,
                     '--yuv', '444', '--cicp', f'9/{transfer}/0', source, encoded])
                run([modules / 'avif/avifdec.exe', '-j', '1', '--depth', '16', encoded, decoded])
            else:
                run([modules / 'heic/heif-enc.exe', '-e', 'x265', '-L', '-b', depth,
                     '-p', 'preset=fast', '-p', 'chroma=444', '-p', 'x265:pools=1', '-p', 'x265:frame-threads=1',
                     '--colour_primaries', '9', '--transfer_characteristic', transfer, '--matrix_coefficients', '0',
                     '-o', encoded, source])
                run([modules / 'heic/heif-dec.exe', '--quiet', '--codec-threads', '1', '--tile-threads', '1', encoded, decoded])
            w, h, actual = read_png16(decoded)
            assert (w, h) == (width, height)
            # Pinned avifdec scales RGB to full16-bit range. libheif's identity
            # RGB path left-aligns native samples (q << (16-depth)), retaining
            # every native bit; do not confuse that with8-bit quantization.
            codec_expected = expected if kind == 'avif' else [
                (q << (16 - depth),) * 3 + (65535,)
                for y in range(height) for q in (range(width) if y % 2 == 0 else range(width - 1, -1, -1))]
            check_precision(actual, codec_expected)
            for mode in ['nearest', 'truncate']:
                # Retain16-bit PNG structure while deliberately losing low bits.
                mutate = lambda v: ((v + 128) // 257 if mode == 'nearest' else v // 257) * 257
                padded = [(mutate(p[0]), mutate(p[1]), mutate(p[2]), p[3]) for p in actual]
                path = root / f'8bit-{depth}-{transfer}-{kind}-{mode}.png'
                write_png16(path, width, height, padded)
                _, _, reread = read_png16(path)
                try:
                    check_precision(reread, codec_expected)
                except AssertionError:
                    pass
                else:
                    raise AssertionError(f'{kind}/{depth}/{transfer}: checker accepted padded8-bit {mode} mutation')
            print(f'PASS lossless {kind} {depth}-bit transfer{transfer}: all {width} codes; two8-bit mutations rejected', flush=True)
