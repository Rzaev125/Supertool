"""Real raster PDF OCR, mixed pages and optional-module failure contracts."""
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

sys.stdout.reconfigure(encoding='utf-8')

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=Path)
exe = parser.parse_args().exe.resolve()
fixture_parent = Path.cwd() / 'test-output'
fixture_parent.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='ocr-', dir=fixture_parent)) / 'Сканы с пробелами'
root.mkdir()
counter = 0


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def run(source, target=None, options=None, fail=False, executable=exe):
    global counter
    counter += 1
    output = target or root / f'result-{counter}.txt'
    temporary = root / f'work-{counter}'
    command = [str(executable), '--convert', str(source), str(output), str(temporary), 'txt']
    if options is not None:
        settings = root / f'options-{counter}.ini'
        settings.write_text('version=1\n' + options, encoding='utf-8')
        command += ['--options', str(settings)]
    before = digest(source)
    output_before = digest(output) if output.exists() else None
    result = subprocess.run(command, capture_output=True, timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
    assert digest(source) == before, 'source changed'
    assert not temporary.exists(), 'private conversion staging leaked'
    error = Path(str(temporary) + '.error.txt')
    detail = error.read_text('utf-8') if error.exists() else result.stderr.decode('utf-8', errors='replace')
    if fail:
        assert result.returncode != 0, 'expected failure'
        if output_before is None:
            assert not output.exists(), 'failed OCR published partial text'
        else:
            assert digest(output) == output_before, 'existing output changed'
        assert b'SWP1\t100\t' not in result.stdout, 'failure reports completion'
        return detail
    assert result.returncode == 0 and output.is_file(), detail or f'conversion exit {result.returncode}'
    assert b'SWP1\t100\t' in result.stdout, 'completion event missing'
    return output.read_text('utf-8')


font = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 54)


def raster(lines):
    image = Image.new('RGB', (1800, 1400), 'white')
    draw = ImageDraw.Draw(image)
    for index, line in enumerate(lines):
        draw.text((110, 180 + index * 100), line, fill='black', font=font)
    return image


def pdf(path, pages):
    document = canvas.Canvas(str(path), pagesize=(432, 336))
    for page in pages:
        if isinstance(page, str):
            document.setFont('Helvetica', 18)
            document.drawString(30, 240, page)
        elif page is not None:
            document.drawImage(ImageReader(page), 0, 0, width=432, height=336)
        document.showPage()
    document.save()


english = raster(['Portable document converter', 'Invoice number 123456789'])
russian = raster(['Проверка распознавания текста', 'Документ номер 123456789'])
azerbaijani = raster(['Azərbaycan dili', 'Bakı şəhəri', 'Sənəd nömrəsi 123456789', 'Şuşa, çörək, yağ, üzüm və göy.'])
source = root / 'Три языка.pdf'
pdf(source, [english, russian, azerbaijani])
text = run(source)
assert 'Portable document converter' in text, repr(text)
assert 'Проверка распознавания текста' in text, repr(text)
assert 'Azərbaycan' in text and 'Bakı' in text and 'Sənəd' in text, repr(text)
assert all(letter in text.split('\f')[-1] for letter in 'əışçöüğ'), repr(text)
assert text.count('\f') == 2, 'page separators lost'
assert text.index('Portable') < text.index('Проверка') < text.index('Azərbaycan'), 'page order changed'
print('PASS OCR: real English, Russian, Azerbaijani raster pages, Unicode and ordered text', flush=True)

mixed = root / 'Mixed.pdf'
pdf(mixed, ['EMBEDDED FIRST 24680', english, None, 'EMBEDDED LAST 13579'])
text = run(mixed)
assert 'EMBEDDED FIRST 24680' in text and 'EMBEDDED LAST 13579' in text and 'Portable' in text
assert text.index('FIRST') < text.index('Portable') < text.index('LAST')
assert text.count('\f') == 3, 'blank page position lost'
assert 'OCR' in run(source, options='pdf_ocr=0\n', fail=True), 'disabled OCR error not actionable'
print('PASS OCR: mixed embedded/raster/blank page order and explicit disable', flush=True)

garbage_layer = root / 'Replacement text layer.pdf'
pdfmetrics.registerFont(TTFont('FixtureArial', r'C:\Windows\Fonts\arial.ttf'))
document = canvas.Canvas(str(garbage_layer), pagesize=(432, 336))
document.drawImage(ImageReader(english), 0, 0, width=432, height=336)
document.setFont('FixtureArial', 12)
document.setFillColorRGB(1, 1, 1)
document.drawString(4, 4, '\ufffd')
document.showPage()
document.save()
assert 'Portable document converter' in run(garbage_layer), 'replacement-only text layer blocks OCR'
print('PASS OCR: replacement-only embedded text falls back to recognition', flush=True)

blank = root / 'Blank.pdf'
pdf(blank, [None])
run(blank, fail=True)
existing = root / 'existing.txt'
existing.write_text('foreign result', encoding='utf-8')
run(source, target=existing, fail=True)
run(source, options='ocr_languages=eng+../../file\n', fail=True)
print('PASS OCR: blank refusal, collisions, invalid language rejection and preservation', flush=True)

isolated = root / 'only-pdfium'
(isolated / 'modules/pdfium').mkdir(parents=True)
shutil.copy2(exe, isolated / exe.name)
shutil.copy2(exe.parent / 'modules/pdfium/pdfium.dll', isolated / 'modules/pdfium/pdfium.dll')
born_digital = root / 'Digital.pdf'
pdf(born_digital, ['NO OCR MODULE NEEDED', None])
assert 'NO OCR MODULE NEEDED' in run(born_digital, executable=isolated / exe.name)
detail = run(source, executable=isolated / exe.name, fail=True)
assert 'OCR' in detail, detail
print('PASS OCR: born-digital extraction without OCR module; scanned input fails clearly without it', flush=True)
shutil.copytree(exe.parent / 'modules/ocr', isolated / 'modules/ocr')
text = run(source, executable=isolated / exe.name)
assert 'Проверка распознавания текста' in text and 'Azərbaycan' in text
(isolated / 'modules/ocr/tessdata/aze_cyrl.traineddata').unlink()
assert 'aze_cyrl' in run(source, executable=isolated / exe.name, fail=True), 'missing Azerbaijani companion is not diagnosed'
english_only = root / 'English.pdf'
pdf(english_only, [english])
assert 'Portable document converter' in run(english_only, options='ocr_languages=eng\n', executable=isolated / exe.name)
print('PASS OCR: Unicode portable module path, selected language and required Azerbaijani companion', flush=True)
print(f'Fixtures: {root}', flush=True)
