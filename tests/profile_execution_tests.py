"""Real conversions using production profile admission, Worker and execution facts."""
import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from PIL import Image


def run(args, timeout=180):
    r = subprocess.run(list(map(str, args)), capture_output=True, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
    assert r.returncode == 0, r.stderr.decode('utf-8', errors='replace')
    return r.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True, type=Path)
    args = parser.parse_args()
    probe = args.probe.resolve()
    ffmpeg = probe.parent / 'modules/ffmpeg/ffmpeg.exe'
    parent = Path.cwd() / 'test-output'
    parent.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='p-', dir=parent)).resolve()
    try:
        spec = importlib.util.spec_from_file_location('profile_fixtures', Path(__file__).resolve().parents[1] / 'scripts/benchmark.py')
        fixtures = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixtures)
        fixtures.fixtures(root, ffmpeg)
        run([ffmpeg, '-v', 'error', '-i', root / 'video.mp4', '-i', root / 'audio.wav', '-shortest', '-c:v', 'copy', '-c:a', 'aac', root / 'video-audio.mp4'])
        originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
        results = root
        measured = []

        def convert(source, target, *extra):
            dest = results / f'o{len(measured)}'
            dest.mkdir()
            rows = json.loads(run([probe, root / source, target, dest, *extra]))
            for row in rows:
                assert row['success'] and row['profile'] == 'Профиль Bakı' and row['elapsed_ms'] > 0, row
                output = Path(row['output'])
                size = sum(p.stat().st_size for p in output.iterdir()) if output.is_dir() else output.stat().st_size
                assert row['output_bytes'] == size and row['stages'], row
                measured.append(row)
            return rows

        rows = convert('image.png', 'jpg')
        with Image.open(rows[0]['output']) as image:
            assert image.size == (640, 400) and image.format == 'JPEG'
        assert rows[0]['stages'] == ['wic'] and not rows[0]['backend']
        rows = convert('video.mp4', 'mp4')
        data = json.loads(run([ffmpeg.with_name('ffprobe.exe'), '-v', 'error', '-show_streams', '-of', 'json', rows[0]['output']]))
        video = next(s for s in data['streams'] if s['codec_type'] == 'video')
        assert video['height'] == 240 and video['codec_name'] == 'h264'
        assert rows[0]['encoder'] == 'libx264' and rows[0]['backend'] == 'cpu'
        run([ffmpeg, '-v', 'error', '-i', rows[0]['output'], '-f', 'null', '-'])
        rows = convert('video-audio.mp4', 'mp3')
        data = json.loads(run([ffmpeg.with_name('ffprobe.exe'), '-v', 'error', '-show_streams', '-of', 'json', rows[0]['output']]))
        assert len(data['streams']) == 1 and data['streams'][0]['codec_type'] == 'audio' and int(data['streams'][0]['bit_rate']) == 160000
        assert rows[0]['encoder'] == 'libmp3lame' and rows[0]['backend'] == 'cpu'
        run([ffmpeg, '-v', 'error', '-i', rows[0]['output'], '-f', 'null', '-'])
        rows = convert('audio.wav', 'mp3')
        assert rows[0]['encoder'] == 'libmp3lame' and rows[0]['backend'] == 'cpu'
        run([ffmpeg, '-v', 'error', '-i', rows[0]['output'], '-f', 'null', '-'])
        for source in ('document.docx', 'workbook.xlsx', 'slides.pptx'):
            rows = convert(source, 'pdf')
            assert 'libreoffice' in rows[0]['stages']
            fixtures.inspect_output(root / source, Path(rows[0]['output']), 'pdf', ffmpeg)
        for source in ('text.pdf', 'scan.pdf'):
            rows = convert(source, 'txt')
            assert 'pdfium' in rows[0]['stages']
            assert ('tesseract' in rows[0]['stages']) == (source == 'scan.pdf')
            fixtures.inspect_output(root / source, Path(rows[0]['output']), 'txt', ffmpeg)
        rows = convert('text.pdf', 'png')
        assert 'pdfium' in rows[0]['stages'] and 'wic' in rows[0]['stages']
        assert len(list(Path(rows[0]['output']).glob('*.png'))) == 1
        rows = convert('image.png', 'jpg', root / 'image.png')
        assert len(rows) == 2 and Path(rows[0]['output']).parent == Path(rows[1]['output']).parent
        assert Path(rows[0]['output']).read_bytes() == Path(rows[1]['output']).read_bytes()
        assert originals == {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
        assert not list(root.rglob('.shiftwheel-*')), 'worker staging leaked'
        print(f'PASS {len(measured)} actual profile conversions: all categories, grouped snapshot, OCR, published bytes/engines, decode and source hashes')
    except BaseException:
        print(f'Profile fixture retained: {root}', flush=True)
        raise
    else:
        shutil.rmtree(root)


if __name__ == '__main__':
    main()
