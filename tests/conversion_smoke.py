"""Four real small-file conversions, decoded independently after publication."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image
from docx import Document
from pypdf import PdfReader

sys.stdout.reconfigure(encoding='utf-8')
parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=Path)
exe = parser.parse_args().exe.resolve()
work = Path.cwd() / 'test-output/smoke'
work.mkdir(parents=True, exist_ok=False)
ffmpeg, ffprobe = [exe.parent / 'modules/ffmpeg' / (n + '.exe') for n in ['ffmpeg', 'ffprobe']]


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    return result


def convert(source, target):
    digest = hashlib.sha256(source.read_bytes()).digest()
    output, temporary = work / ('result-' + source.stem + '.' + target), work / ('work-' + source.stem)
    result = subprocess.run([str(exe), '--convert', str(source), str(output), str(temporary), target],
                            capture_output=True, timeout=90, creationflags=subprocess.CREATE_NO_WINDOW)
    error = Path(str(temporary) + '.error.txt')
    assert result.returncode == 0 and output.is_file(), error.read_text('utf-8') if error.exists() else str(result.returncode)
    assert hashlib.sha256(source.read_bytes()).digest() == digest and not temporary.exists()
    return output


picture = work / 'image.png'
Image.new('RGB', (64, 48), (210, 30, 50)).save(picture)
with Image.open(convert(picture, 'jpg')) as decoded:
    assert decoded.size == (64, 48) and decoded.getpixel((20, 20))[0] > 190
print('PASS image PNG -> JPG: dimensions, pixels, original preserved', flush=True)
video, audio = work / 'video.mov', work / 'audio.wav'
run([ffmpeg, '-hide_banner', '-nostdin', '-loglevel', 'error', '-f', 'lavfi', '-i', 'color=c=red:s=96x64:r=10:d=1',
     '-c:v', 'libx264', '-threads', '2', video])
run([ffmpeg, '-hide_banner', '-nostdin', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=1', audio])
for source, target, kind in [(video, 'mp4', 'video'), (audio, 'mp3', 'audio')]:
    output = convert(source, target)
    data = json.loads(run([ffprobe, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', output]).stdout)
    assert [s['codec_type'] for s in data['streams']] == [kind]
    assert .8 < float(data['format']['duration']) < 1.5
    run([ffmpeg, '-v', 'error', '-xerror', '-i', output, '-f', 'null', '-'])
    print('PASS ' + kind + ': stream, duration, complete decode, original preserved', flush=True)
document = work / 'document.docx'
doc = Document()
doc.add_paragraph('Supertool local check. Hello, Baku!')
doc.save(document)
pages = PdfReader(convert(document, 'pdf')).pages
assert len(pages) == 1 and 'Supertool local check' in pages[0].extract_text()
print('PASS document DOCX -> PDF: page count, text, original preserved', flush=True)
