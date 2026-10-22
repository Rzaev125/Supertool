"""Real converter output checks for immutable options snapshots and presets."""
import argparse
import hashlib
import json
import random
import subprocess
import tempfile
from pathlib import Path
from PIL import Image
from reportlab.pdfgen import canvas

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=Path)
args = parser.parse_args()
exe = args.exe.resolve()
ffmpeg = exe.parent / 'modules' / 'ffmpeg' / 'ffmpeg.exe'
ffprobe = exe.parent / 'modules' / 'ffmpeg' / 'ffprobe.exe'
root = Path(tempfile.mkdtemp(prefix='presets-', dir=Path.cwd()))
counter = 0
progress = {}

def run(command):
    return subprocess.run([str(x) for x in command], capture_output=True, timeout=180,
                          creationflags=subprocess.CREATE_NO_WINDOW)

def convert(source, target, fail=False, **options):
    global counter
    counter += 1
    destination = root / (f'output-{counter}' if source.suffix == '.pdf' and target in ['png', 'jpg'] else f'output-{counter}.{target}')
    temporary = root / f'work-{counter}'
    snapshot = root / f'options-{counter}.ini'
    snapshot.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k, v in options.items()), encoding='utf-8')
    before = hashlib.sha256(source.read_bytes()).digest()
    result = run([exe, '--convert', source, destination, temporary, target, '--options', snapshot])
    progress[destination] = [line.split('\t', 2) for line in result.stdout.decode('utf-8', errors='replace').splitlines() if line.startswith('SWP1\t')]
    error = Path(str(temporary) + '.error.txt')
    detail = error.read_text('utf-8', errors='replace') if error.exists() else result.stderr.decode(errors='replace')
    assert hashlib.sha256(source.read_bytes()).digest() == before, 'source changed'
    assert not temporary.exists(), 'temporary conversion files leaked'
    assert snapshot.exists(), 'converter deleted caller-owned snapshot'
    if fail:
        assert result.returncode != 0 and not destination.exists(), 'invalid options created output'
    else:
        assert result.returncode == 0 and destination.exists(), detail or f'conversion exit {result.returncode}'
    return destination

def probe(path):
    result = run([ffprobe, '-v', 'error', '-show_streams', '-of', 'json', path])
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)['streams']

source = root / 'alpha.png'
image = Image.new('RGBA', (384, 192), (200, 60, 30, 128))
image.save(source)
for target in ['png', 'tiff', 'webp', 'avif']:
    output = convert(source, target, image_max_dimension=96)
    # Decode AVIF through the supported converter because Pillow AVIF varies by runtime.
    inspect = convert(output, 'png') if target == 'avif' else output
    with Image.open(inspect) as img:
        assert img.size == (96, 48), (target, img.size)
        assert abs(img.convert('RGBA').getpixel((48, 24))[3] - 128) <= 1, f'{target} lost alpha'
small = convert(source, 'png', image_max_dimension=1000)
assert Image.open(small).size == (384, 192), 'image upscaled'

randomizer = random.Random(7)
noise = root / 'noise.png'
Image.frombytes('RGB', (512, 256), randomizer.randbytes(512 * 256 * 3)).save(noise)
compact = convert(noise, 'jpg', quality='compact')
high = convert(noise, 'jpg', quality='high')
assert high.stat().st_size > compact.stat().st_size * 1.3, 'JPEG quality preset ignored'
for target in ['avif', 'webp']:
    compact = convert(noise, target, quality='compact')
    high = convert(noise, target, quality='high')
    assert high.stat().st_size > compact.stat().st_size * 1.3, f'{target} quality preset ignored'
for bad in [{'pdf_dpi': 0}, {'image_max_dimension': 32769}, {'resources': 'arbitrary'}, {'audio_bitrate_kbps': 9999}]:
    convert(source, 'png', fail=True, **bad)
print('PASS presets: image aspect, no upscale, alpha in PNG/TIFF/WebP/AVIF, JPEG/AVIF/WebP quality, invalid refusal', flush=True)

pdf = root / 'page.pdf'
document = canvas.Canvas(str(pdf), pagesize=(200, 160))
for number in [1, 2]:
    document.drawString(10, 10, f'PDF DPI page {number}'); document.showPage()
document.save()
pages = convert(pdf, 'png', pdf_dpi=72)
assert Image.open(next(pages.glob('*.png'))).size == (200, 160), 'PDF DPI ignored'
page_progress = [int(event[1]) for event in progress[pages] if event[2].startswith('Страница PDF ')]
assert len(page_progress) == 2 and 0 < page_progress[0] < page_progress[1] < 100, 'PDF progress does not advance per page before publication'
print('PASS presets: PDF raster DPI and per-page progress', flush=True)

video = root / 'video.mp4'
result = run([ffmpeg, '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x240:rate=12:duration=2',
              '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=2', '-c:v', 'libx264', '-c:a', 'aac', '-shortest', video])
assert result.returncode == 0, result.stderr
small_video = convert(video, 'mp4', video_max_height=120, resources='economy')
stream = next(s for s in probe(small_video) if s['codec_type'] == 'video')
assert (stream['width'], stream['height']) == (160, 120), 'video height/aspect ignored'
audio = convert(video, 'mp3', audio_bitrate_kbps=64, quality='high')
stream = next(s for s in probe(audio) if s['codec_type'] == 'audio')
assert int(stream['bit_rate']) == 64000, 'explicit audio bitrate did not override preset'
for quality, expected in [('compact', 128000), ('balanced', 192000), ('high', 320000)]:
    audio = convert(video, 'mp3', quality=quality)
    stream = next(s for s in probe(audio) if s['codec_type'] == 'audio')
    assert int(stream['bit_rate']) == expected, 'audio preset bitrate ignored'
compact_video = convert(video, 'mp4', quality='compact')
high_video = convert(video, 'mp4', quality='high')
assert high_video.stat().st_size > compact_video.stat().st_size, 'video preset ignored'
print('PASS presets: video aspect/height, audio bitrate override, video quality', flush=True)
print(f'Fixtures: {root}', flush=True)
