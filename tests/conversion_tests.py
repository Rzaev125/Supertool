"""Real end-to-end converters. Python packages are used for fixtures/inspection only."""
import argparse
import csv
import hashlib
import json
import subprocess
import sys
import shutil
import tempfile
import zipfile
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True, type=Path)
parser.add_argument('--group', choices=['all', 'pdf', 'office', 'media', 'avif', 'formats'], default='all')
args = parser.parse_args()
exe = args.exe.resolve()
root = Path(tempfile.mkdtemp(prefix='conversions-', dir=Path.cwd() / 'test-output')) / 'Тест с пробелами'
root.mkdir()
print(f'Fixtures: {root}', flush=True)

def run(command, timeout=150):
    return subprocess.run([str(x) for x in command], capture_output=True, timeout=timeout,
                          creationflags=subprocess.CREATE_NO_WINDOW)

def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()

def convert(source, target, destination=None, fail=False):
    destination = destination or root / f'{source.stem}-result.{target}'
    temp = destination.parent / f'work-{destination.name}'
    before = digest(source)
    process = run([exe, '--convert', source, destination, temp, target])
    error = Path(str(temp) + '.error.txt')
    detail = error.read_text('utf-8', errors='replace') if error.exists() else ''
    assert digest(source) == before, 'source changed'
    assert not temp.exists(), f'staging left behind: {temp}'
    if fail:
        assert process.returncode != 0 and not destination.exists(), detail
    else:
        assert process.returncode == 0 and destination.exists(), detail
    return destination

def pdf_tests():
    from reportlab.pdfgen import canvas
    from PIL import Image
    from pypdf import PdfReader, PdfWriter
    source = root / 'Две страницы.pdf'
    c = canvas.Canvas(str(source), pagesize=(200, 160))
    for color, text in [((1, 0, 0), 'First page'), ((0, 0, 1), 'Second page')]:
        c.setFillColorRGB(*color); c.rect(0, 0, 200, 160, fill=1, stroke=0)
        c.setFillColorRGB(1, 1, 1); c.drawString(20, 90, text); c.showPage()
    c.save()
    for target in ['png', 'jpg']:
        dest = convert(source, target, root / f'pages-{target}')
        images = sorted(dest.glob(f'*.{target}'))
        assert len(images) == 2, 'later PDF pages missing'
        with Image.open(images[0]) as image:
            assert image.size == (400, 320), image.size
            r, g, b = image.convert('RGB').getpixel((10, 10))
            assert r > 240 and b < 15 and g < 15, 'first page rendering'
        with Image.open(images[1]) as image:
            r, g, b = image.convert('RGB').getpixel((10, 10))
            assert b > 240 and r < 15 and g < 15, 'second page rendering'
    text = convert(source, 'txt').read_text('utf-8')
    assert 'First page' in text and 'Second page' in text, text
    existing = root / 'existing pages'; existing.mkdir(); sentinel = existing / 'keep.txt'; sentinel.write_text('keep')
    temporary = root / 'collision-work'
    proc = run([exe, '--convert', source, existing, temporary, 'png'])
    assert proc.returncode != 0 and sentinel.read_text() == 'keep' and not temporary.exists()
    encrypted = root / 'encrypted.pdf'
    writer = PdfWriter(); writer.append(PdfReader(source)); writer.encrypt('password'); writer.write(encrypted)
    convert(encrypted, 'png', root / 'encrypted-pages', fail=True)
    blank = root / 'scanned.pdf'; c = canvas.Canvas(str(blank)); c.rect(20, 20, 40, 40, fill=1); c.showPage(); c.save()
    convert(blank, 'txt', fail=True)
    corrupt = root / 'broken.pdf'; corrupt.write_bytes(b'not a PDF'); convert(corrupt, 'png', root / 'bad-pages', fail=True)
    foreign_temp = root / 'foreign-temp'; foreign_temp.mkdir(); keep = foreign_temp / 'keep'; keep.write_bytes(b'keep')
    proc = run([exe, '--convert', source, root / 'no-output.txt', foreign_temp, 'txt'])
    assert proc.returncode != 0 and keep.read_bytes() == b'keep' and not (root / 'no-output.txt').exists()
    isolated = root / 'no-modules'; isolated.mkdir(); shutil.copy2(exe, isolated / exe.name)
    temp = root / 'missing-engine-work'
    proc = run([isolated / exe.name, '--convert', source, root / 'missing-output.txt', temp, 'txt'])
    assert proc.returncode != 0 and not (root / 'missing-output.txt').exists() and not temp.exists()
    assert 'Модуль' in Path(str(temp) + '.error.txt').read_text('utf-8')
    form = root / 'filled-form.pdf'; c = canvas.Canvas(str(form))
    c.drawString(20, 700, 'Form value must not disappear')
    c.acroForm.textfield(name='name', value='VISIBLE VALUE', x=20, y=650, width=250, height=30)
    c.showPage(); c.save()
    convert(form, 'png', root / 'form-pages', fail=True)
    convert(form, 'txt', fail=True)
    print('PASS PDF: all pages, 144 DPI pixels, full text, encrypted/scanned/corrupt refusal, exclusive folder publication', flush=True)

def office_tests():
    from docx import Document
    from pypdf import PdfReader
    import xlsxwriter
    doc = root / 'Документ.docx'
    document = Document(); document.add_heading('ShiftWheel document', 0)
    document.add_paragraph('First document page. Привет мир.')
    document.add_page_break(); document.add_paragraph('Second document page.'); document.save(doc)
    for target in ['pdf', 'docx', 'odt', 'rtf', 'txt']:
        dest = convert(doc, target)
        if target == 'pdf':
            pdf = PdfReader(dest); assert len(pdf.pages) == 2
            assert 'Second document page' in pdf.pages[1].extract_text()
        elif target == 'txt':
            text = dest.read_text('utf-8-sig'); assert 'Привет мир' in text and 'Second document page' in text
        elif target in ['docx', 'odt']:
            with zipfile.ZipFile(dest) as z:
                xml = z.read('word/document.xml' if target == 'docx' else 'content.xml').decode()
                assert 'Second document page' in xml
        else:
            assert dest.read_bytes().startswith(b'{\\rtf')
    book = root / 'Таблица.xlsx'
    with xlsxwriter.Workbook(book) as workbook:
        for name, value in [('First', 42), ('Второй', 73)]:
            sheet = workbook.add_worksheet(name); sheet.write_row(0, 0, ['Name', 'Value'])
            sheet.write_row(1, 0, [name, value]); sheet.write_formula(2, 1, '=B2+1', None, value+1)
    for target in ['pdf', 'xlsx', 'ods', 'csv']:
        dest = convert(book, target, root / 'all sheets' if target == 'csv' else None)
        if target == 'pdf':
            pdf = PdfReader(dest); assert len(pdf.pages) == 2
            text = '\n'.join(p.extract_text() for p in pdf.pages); assert '42' in text and '73' in text
        elif target == 'csv':
            files = sorted(dest.glob('*.csv')); assert len(files) == 2, [f.name for f in files]
            rows = [list(csv.reader(f.read_text('utf-8-sig').splitlines())) for f in files]
            assert any(r[1][1] == '42' and r[2][1] == '43' for r in rows), rows
            assert any(r[1][0] == 'Второй' and r[1][1] == '73' and r[2][1] == '74' for r in rows), rows
        else:
            with zipfile.ZipFile(dest) as z:
                xml = z.read('xl/workbook.xml' if target == 'xlsx' else 'content.xml').decode()
                assert 'First' in xml and 'Второй' in xml
    fake = root / 'fake.docx'; fake.write_bytes(b'plain text pretending to be docx'); convert(fake, 'pdf', fail=True)
    print('PASS Office: DOCX five targets, XLSX four targets, both PDF pages, both CSV sheets, formulas as values, Unicode, corrupt refusal', flush=True)

def media_tests():
    ffmpeg = exe.parent / 'modules/ffmpeg/ffmpeg.exe'
    ffprobe = exe.parent / 'modules/ffmpeg/ffprobe.exe'
    def ff(*options):
        p = run([ffmpeg, '-hide_banner', '-nostdin', '-loglevel', 'error', *options])
        assert p.returncode == 0, p.stderr
    def streams(path):
        p = run([ffprobe, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', path])
        assert p.returncode == 0, p.stderr
        data = json.loads(p.stdout); assert .8 < float(data['format']['duration']) < 1.5
        return [s['codec_type'] for s in data['streams']]
    video = root / 'Клип с музыкой.mov'
    ff('-f', 'lavfi', '-i', 'color=c=red:s=96x64:r=10:d=1', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=1', '-c:v', 'libx264', '-threads', '2', '-c:a', 'aac', '-shortest', video)
    for target in ['mp4', 'mkv', 'webm', 'mp3', 'wav']:
        dest = convert(video, target)
        kinds = streams(dest)
        assert kinds == (['video', 'audio'] if target in ['mp4', 'mkv', 'webm'] else ['audio']), kinds
    # Keep the valid container, but corrupt a non-key H.264 packet's NAL length.
    # Default FFmpeg tolerance skips it and can still return success.
    packet_info = run([ffprobe, '-v', 'error', '-select_streams', 'v:0', '-show_packets', '-show_entries', 'packet=pos,size', '-of', 'json', video])
    packets = json.loads(packet_info.stdout)['packets']; assert len(packets) > 2
    damaged = bytearray(video.read_bytes()); position = int(packets[1]['pos'])
    damaged[position:position + 4] = b'\x7f\xff\xff\xff'
    corrupt_packet = root / 'bad-packet.mov'; corrupt_packet.write_bytes(damaged)
    convert(corrupt_packet, 'mp4', fail=True)
    audio = root / 'Музыка.wav'; ff('-f', 'lavfi', '-i', 'sine=frequency=660:duration=1', '-c:a', 'pcm_s16le', audio)
    for target in ['mp3', 'wav', 'flac', 'm4a', 'ogg']:
        assert streams(convert(audio, target)) == ['audio']
    silent = root / 'silent.mp4'; ff('-f', 'lavfi', '-i', 'color=s=96x64:r=10:d=1', '-c:v', 'libx264', '-threads', '2', silent)
    assert streams(convert(silent, 'mkv')) == ['video']
    convert(silent, 'mp3', fail=True)
    bad = root / 'broken.mp4'; bad.write_bytes(b'not a video'); convert(bad, 'webm', fail=True)
    existing = root / 'existing.mp3'; existing.write_bytes(b'keep'); temp = root / 'media-collision'
    proc = run([exe, '--convert', audio, existing, temp, 'mp3'])
    assert proc.returncode != 0 and existing.read_bytes() == b'keep' and not temp.exists()
    print('PASS media: five video/audio targets, stream contents/duration, video without audio, malformed input, no overwrite', flush=True)

def avif_tests():
    from PIL import Image, ImageCms
    source = root / 'Прозрачное фото.png'
    image = Image.new('RGBA', (64, 48), (255, 0, 0, 255))
    image.paste((255, 0, 0, 0), (0, 0, 16, 48))
    image.paste((255, 0, 0, 128), (16, 0, 32, 48))
    image.save(source)
    avif = convert(source, 'avif')
    with Image.open(avif) as decoded:
        assert decoded.format == 'AVIF' and decoded.size == (64, 48)
        rgba = decoded.convert('RGBA')
        assert [rgba.getpixel((x, 24))[3] for x in [8, 24, 48]] == [0, 128, 255], 'alpha channel changed'
        assert rgba.getpixel((48, 24))[0] > 240 and rgba.getpixel((48, 24))[1] < 15
    for target in ['png', 'jpg', 'bmp', 'avif']:
        result = convert(avif, target, root / f'decoded.{target}')
        with Image.open(result) as decoded:
            assert decoded.size == (64, 48)
            if target in ['png', 'avif']:
                assert [decoded.convert('RGBA').getpixel((x, 24))[3] for x in [8, 24, 48]] == [0, 128, 255]
            else:
                rgb = decoded.convert('RGB')
                assert min(rgb.getpixel((8, 24))) > 240, 'transparent area must be white'
                r, g, b = rgb.getpixel((24, 24))
                assert r > 240 and 110 < g < 145 and 110 < b < 145, 'partial alpha must blend with white'
    for extension in ['jpg', 'bmp']:
        plain = root / f'plain-{extension}.{extension}'; image.convert('RGB').save(plain)
        with Image.open(convert(plain, 'avif')) as decoded:
            assert decoded.format == 'AVIF' and decoded.size == image.size
    corrupt = root / 'broken.avif'; corrupt.write_bytes(b'not an AVIF')
    convert(corrupt, 'png', fail=True)
    truncated = root / 'truncated.avif'; truncated.write_bytes(avif.read_bytes()[:64])
    convert(truncated, 'png', fail=True)
    disguised = root / 'disguised.avif'; disguised.write_bytes(source.read_bytes())
    convert(disguised, 'png', fail=True)
    animated = root / 'animated.avif'
    image.save(animated, save_all=True, append_images=[Image.new('RGBA', image.size, (0, 255, 0, 255))], duration=100)
    convert(animated, 'png', fail=True)
    encoder = exe.parent / 'modules/avif/avifenc.exe'
    rotated = root / 'rotated.avif'
    encoded = run([encoder, '-j', '2', '-s', '8', '--irot', '1', source, rotated])
    assert encoded.returncode == 0, encoded.stderr
    with Image.open(convert(rotated, 'png')) as decoded:
        assert decoded.size == (48, 64)
    for name, options in [('hdr', ['--cicp', '9/16/9']),
                          ('linear', ['--cicp', '1/8/6']), ('wide-gamut', ['--cicp', '12/13/6'])]:
        special = root / f'{name}.avif'
        encoded = run([encoder, '-j', '2', '-s', '8', *options, source, special])
        assert encoded.returncode == 0, encoded.stderr
        convert(special, 'png', fail=True)
    # A linear RGB ICC profile: sRGB primaries, gamma=1 TRCs. Linear 0.5 must
    # become approximately sRGB 0.735 (~188/255), not silently remain 128.
    import struct
    profile = bytearray(ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes())
    for index in range(struct.unpack_from('>I', profile, 128)[0]):
        record = 132 + index * 12
        if profile[record:record+4] in [b'rTRC', b'gTRC', b'bTRC']:
            offset = struct.unpack_from('>I', profile, record + 4)[0]
            struct.pack_into('>I', profile, record + 8, 16)
            profile[offset:offset+16] = b'para' + bytes(8) + struct.pack('>i', 65536)
    profile[84:100] = bytes(16)  # no cached profile ID after modifying the curve
    gray = Image.new('RGBA', (32, 32), (128, 128, 128, 128))
    profiled_png = root / 'linear-profile.png'; gray.save(profiled_png, icc_profile=bytes(profile))
    profiled_avif = root / 'linear-profile.avif'
    encoded = run([encoder, '-j', '2', '-s', '8', profiled_png, profiled_avif])
    assert encoded.returncode == 0, encoded.stderr
    for source_profiled, target in [(profiled_png, 'avif'), (profiled_avif, 'png'), (profiled_avif, 'avif')]:
        result = convert(source_profiled, target, root / f'managed-{source_profiled.suffix[1:]}.{target}')
        with Image.open(result) as decoded:
            r, g, b, alpha = decoded.convert('RGBA').getpixel((16, 16))
            assert 180 <= r <= 195 and 180 <= g <= 195 and 180 <= b <= 195, (r, g, b)
            assert alpha == 128, 'color management must preserve alpha'
    existing = root / 'existing.avif'; existing.write_bytes(b'keep')
    temporary = root / 'collision-work'
    proc = run([exe, '--convert', source, existing, temporary, 'avif'])
    assert proc.returncode != 0 and existing.read_bytes() == b'keep' and not temporary.exists()
    foreign = root / 'foreign-work'; foreign.mkdir(); (foreign / 'keep').write_bytes(b'keep')
    proc = run([exe, '--convert', source, root / 'absent.avif', foreign, 'avif'])
    assert proc.returncode != 0 and (foreign / 'keep').read_bytes() == b'keep' and not (root / 'absent.avif').exists()
    isolated = root / 'no-modules-avif'; isolated.mkdir(); shutil.copy2(exe, isolated / exe.name)
    missing_temp = root / 'missing-work'
    proc = run([isolated / exe.name, '--convert', source, root / 'missing.avif', missing_temp, 'avif'])
    assert proc.returncode != 0 and not (root / 'missing.avif').exists() and not missing_temp.exists()
    assert 'Модуль' in Path(str(missing_temp) + '.error.txt').read_text('utf-8')
    # Built-in image routes must still work without optional engine modules.
    proc = run([isolated / exe.name, '--convert', source, root / 'builtin.jpg', root / 'builtin.tmp', 'jpg'])
    assert proc.returncode == 0 and (root / 'builtin.jpg').exists()
    print('PASS AVIF: PNG/JPEG/BMP encode, four decode targets, alpha/white background, Unicode, original preservation, corrupt/animated/rotated/HDR refusal, exclusive publication, missing engine', flush=True)

def formats_tests():
    from PIL import Image, ImageCms, TiffImagePlugin
    import struct
    source = root / 'Прозрачные цвета.png'
    image = Image.new('RGBA', (64, 48), (210, 40, 80, 255))
    for x in range(32):
        for y in range(48): image.putpixel((x, y), (210, 40, 80, 0 if x < 16 else 128))
    image.save(source)
    for target in ['webp', 'tiff']:
        result = convert(source, target)
        with Image.open(result) as decoded:
            assert decoded.format == target.upper() and decoded.size == image.size
            rgba = decoded.convert('RGBA')
            assert rgba.getpixel((20, 20)) == (210, 40, 80, 128), (target, rgba.getpixel((20,20)))
            assert rgba.getpixel((40, 20)) == (210, 40, 80, 255)
            assert rgba.getpixel((5, 20))[3] == 0
        for other in ['png', 'jpg', 'bmp', 'avif', 'webp', 'tiff']:
            output = convert(result, other, root / f'from-{target}.{other}')
            with Image.open(output) as decoded:
                assert decoded.size == image.size
                rgba = decoded.convert('RGBA')
                if other in ['jpg', 'bmp']:
                    assert min(rgba.getpixel((5,20))[:3]) > 248
                    assert all(abs(a-b) <= 5 for a,b in zip(rgba.getpixel((20,20))[:3], (232,147,167)))
                else:
                    assert rgba.getpixel((5,20))[3] == 0 and rgba.getpixel((20,20))[3] == 128
        avif = root / f'from-{target}.avif'
        convert(avif, target, root / f'avif-to-{target}.{target}')
    for source_type in ['jpg', 'bmp']:
        plain = root / f'plain-{source_type}.{source_type}'; image.convert('RGB').save(plain)
        for target in ['webp','tiff']: convert(plain, target)
    alias = root / 'alias.TIF'; image.save(alias, format='TIFF'); convert(alias, 'tif')
    # Independent fixtures: lossy WebP and big-endian TIFF.
    lossy = root / 'lossy.webp'; image.save(lossy, quality=85); convert(lossy, 'png')
    big_endian = root / 'big-endian.tiff'; image.convert('RGB').save(big_endian, byteorder='MM'); convert(big_endian, 'png')
    # Linear ICC mid-grey must be converted to sRGB (~188), not copied as 128.
    profile = bytearray(ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes())
    for n in range(int.from_bytes(profile[128:132], 'big')):
        entry = 132 + n * 12
        if profile[entry:entry+4] in [b'rTRC', b'gTRC', b'bTRC']:
            offset = int.from_bytes(profile[entry+4:entry+8], 'big')
            profile[offset:offset+16] = b'para' + b'\0'*8 + b'\0\1\0\0'
    for source_type in ['webp', 'tiff']:
        profiled = root / f'linear.{source_type}'
        Image.new('RGBA',(64,48),(128,128,128,128)).save(profiled, icc_profile=bytes(profile), lossless=True)
        for target in ['png', 'webp', 'tiff', 'avif']:
            with Image.open(convert(profiled,target,root/f'profile-{source_type}.{target}')) as decoded:
                pixel = decoded.convert('RGBA').getpixel((20,20))
                assert all(180 <= c <= 195 for c in pixel[:3]) and pixel[3] == 128, (source_type,target,pixel)
    animated = root / 'animated.webp'
    image.save(animated, save_all=True, append_images=[Image.new('RGBA',image.size,'blue')], duration=100, loop=0)
    convert(animated, 'png')
    multipage = root / 'multipage.tiff'; image.save(multipage,save_all=True,append_images=[image])
    convert(multipage, 'png', fail=True)
    for kind in ['webp','tiff']:
        rotated = root / f'rotated-{kind}.{kind}'
        if kind == 'webp':
            exif = Image.Exif(); exif[274] = 6; image.save(rotated,exif=exif)
        else:
            tags = TiffImagePlugin.ImageFileDirectory_v2(); tags[274] = 6; image.save(rotated,tiffinfo=tags)
        assert Image.open(convert(rotated, 'png')).size == (image.height, image.width), 'EXIF rotation lost'
        bad = root / f'bad.{kind}'; bad.write_bytes(b'not an image'); convert(bad,'png',fail=True)
        wrong = root / f'disguised.{kind}'; wrong.write_bytes(source.read_bytes()); convert(wrong,'png',fail=True)
    webp = root / 'Прозрачные цвета-result.webp'
    truncated = root / 'truncated.webp'; truncated.write_bytes(webp.read_bytes()[:-5]); convert(truncated,'png',fail=True)
    huge = root / 'huge.webp'
    payload = b'VP8X' + struct.pack('<I',10) + b'\0'*4 + b'\xff'*6
    huge.write_bytes(b'RIFF' + struct.pack('<I',len(payload)+4) + b'WEBP' + payload); convert(huge,'png',fail=True)
    existing = root / 'existing.webp'; existing.write_bytes(b'keep'); temp = root / 'collision-new'
    proc = run([exe,'--convert',source,existing,temp,'webp'])
    assert proc.returncode != 0 and existing.read_bytes() == b'keep' and not temp.exists()
    foreign = root / 'foreign-new'; foreign.mkdir(); (foreign/'keep').write_bytes(b'keep')
    proc = run([exe,'--convert',source,root/'no-new.webp',foreign,'webp'])
    assert proc.returncode != 0 and (foreign/'keep').read_bytes() == b'keep'
    isolated = root / 'no-modules-new'; isolated.mkdir(); shutil.copy2(exe,isolated/exe.name)
    temp = root / 'missing-new'; missing = root / 'missing.webp'
    proc = run([isolated/exe.name,'--convert',source,missing,temp,'webp'])
    assert proc.returncode != 0 and not missing.exists() and not temp.exists()
    assert 'Модуль' in Path(str(temp)+'.error.txt').read_text('utf-8')
    target = root / 'no-module.tiff'
    proc = run([isolated/exe.name,'--convert',source,target,temp,'tiff'])
    assert proc.returncode == 0 and target.exists(), 'TIFF must work without modules'
    ffmpeg = exe.parent / 'modules/ffmpeg/ffmpeg.exe'; ffprobe = exe.parent / 'modules/ffmpeg/ffprobe.exe'
    audio = root / 'Новая музыка.wav'
    proc = run([ffmpeg,'-v','error','-f','lavfi','-i','sine=frequency=660:duration=1','-ar','44100',audio]); assert proc.returncode == 0, proc.stderr
    for target, codec in [('opus','opus'),('aac','aac')]:
        result = convert(audio,target)
        probe = run([ffprobe,'-v','error','-show_streams','-show_format','-of','json',result]); assert probe.returncode == 0
        data = json.loads(probe.stdout); assert len(data['streams']) == 1 and data['streams'][0]['codec_name'] == codec
        assert .9 < float(data['format']['duration']) < 1.2
        convert(result,'wav',root/f'back-{target}.wav')
    video = root / 'Новый клип.mp4'
    proc = run([ffmpeg,'-v','error','-f','lavfi','-i','color=c=red:s=96x64:r=10:d=1','-i',audio,'-c:v','libx264','-threads','2','-c:a','aac','-shortest',video]); assert proc.returncode == 0,proc.stderr
    mov = convert(video,'mov')
    probe = run([ffprobe,'-v','error','-show_streams','-show_format','-of','json',mov]); assert probe.returncode == 0
    data=json.loads(probe.stdout); assert [s['codec_name'] for s in data['streams']] == ['h264','aac']; assert .9 < float(data['format']['duration']) < 1.2
    convert(mov,'mp4',root/'mov-back.mp4')
    print('PASS new formats: WebP/TIFF pixels, alpha, ICC, cross-conversions, refusals, exclusive publication, Opus/AAC/MOV codecs and duration',flush=True)

for group, test in [('pdf', pdf_tests), ('office', office_tests), ('media', media_tests), ('avif', avif_tests), ('formats', formats_tests)]:
    if args.group in ['all', group]: test()
