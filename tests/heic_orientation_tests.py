"""Independent pixel oracle for original HEIC EXIF, container precedence and invalid tags."""
import argparse
import hashlib
import struct
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageChops, ImageCms
from heic_fixture import attach_exif, RESERVED

parser = argparse.ArgumentParser(); parser.add_argument('--exe', type=Path, required=True)
exe = parser.parse_args().exe.resolve(); module = exe.parent / 'modules/heic'
base = Path.cwd() / 'test-output'; base.mkdir(exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='heic-exif-', dir=base)); counter = 0

def run(*command):
    return subprocess.run(list(map(str, command)), capture_output=True, timeout=90,
                          creationflags=subprocess.CREATE_NO_WINDOW)

def convert(source, target='png', fail=False, **options):
    global counter
    counter += 1; output = root / f'out-{counter}.{target}'; work = root / f'work-{counter}'
    snapshot = root / f'options-{counter}.ini'
    snapshot.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k,v in options.items()), encoding='utf-8')
    before = hashlib.sha256(source.read_bytes()).digest()
    result = run(exe, '--convert', source, output, work, target, '--options', snapshot)
    assert hashlib.sha256(source.read_bytes()).digest() == before and not work.exists()
    error = Path(str(work) + '.error.txt')
    detail = error.read_text('utf-8') if error.exists() else result.stderr
    if fail:
        assert result.returncode != 0 and not output.exists(), source
    else:
        assert result.returncode == 0 and output.exists(), detail
    return output

def tiff(value, endian='<', duplicate=False, kind=3, count=1):
    entry = struct.pack(endian+'HHI',274,kind,count) + struct.pack(endian+'H',value)+b'\0\0'
    return (b'II' if endian=='<' else b'MM')+struct.pack(endian+'HIH',42,8,2 if duplicate else 1)+entry*(2 if duplicate else 1)+b'\0'*4

source = root/'chart.png'
image = Image.new('RGBA',(80,48)); image.putdata([(x*3%256,y*5%256,(x+y)*2%256,128 if x>40 else 255) for y in range(48) for x in range(80)])
image.save(source,exif=b'Exif\0\0'+RESERVED)

def encode(name, *extra):
    output=root/name
    result=run(module/'heif-enc.exe','-L','-p','chroma=444','-p','x265:pools=1','-p','x265:frame-threads=1',
               '--colour_primaries','1','--transfer_characteristic','13','--matrix_coefficients','0',*extra,'-o',output,source)
    assert result.returncode==0,result.stderr
    return output

def decode(source):
    output=source.with_suffix('.raw.png')
    result=run(module/'heif-dec.exe','--quiet',source,output); assert result.returncode==0,result.stderr
    return Image.open(output).convert('RGBA')

original=encode('base.heic'); pixels=decode(original)
operations={2:Image.Transpose.FLIP_LEFT_RIGHT,3:Image.Transpose.ROTATE_180,4:Image.Transpose.FLIP_TOP_BOTTOM,
            5:Image.Transpose.TRANSPOSE,6:Image.Transpose.ROTATE_270,7:Image.Transpose.TRANSVERSE,8:Image.Transpose.ROTATE_90}
for endian in ['<','>']:
    for orientation in range(1,9):
        fixture=root/f'exif-{endian==">"}-{orientation}.heic'
        attach_exif(module,original,fixture,tiff(orientation,endian))
        expected=pixels if orientation==1 else pixels.transpose(operations[orientation])
        actual=Image.open(convert(fixture)).convert('RGBA')
        assert actual.size==expected.size and actual.tobytes()==expected.tobytes(),(endian,orientation)

for name,arguments in [('rotate',('--rotate-cw','90')),('mirror',('--flip-h',)),('both',('--rotate-cw','90','--flip-h'))]:
    encoded=encode(name+'.heic',*arguments); fixture=root/(name+'-exif.heic')
    attach_exif(module,encoded,fixture,tiff(6))
    expected=decode(fixture); actual=Image.open(convert(fixture)).convert('RGBA')
    assert actual.size==expected.size and actual.tobytes()==expected.tobytes(),name
    if name=='rotate':
        data=bytearray(fixture.read_bytes()); index=data.index(b'irot'); data[index+4]=0
        identity=root/'identity.heic'; identity.write_bytes(data)
        expected=decode(identity); actual=Image.open(convert(identity)).convert('RGBA')
        assert actual.size==expected.size and actual.tobytes()==expected.tobytes(),'identity property'

for index,payload in enumerate([tiff(0),tiff(9),tiff(6,duplicate=True),tiff(6,kind=4),tiff(6,count=2),b'II*\0'+b'\0'*12]):
    fixture=root/f'bad-{index}.heic'; attach_exif(module,original,fixture,payload); convert(fixture,fail=True)

# Every bridge sees already oriented pixels, followed by resize exactly once.
fixture=root/'bridge.heic'; attach_exif(module,original,fixture,tiff(6))
for target in ['tiff','bmp','jpg','webp','avif','heic','gif']:
    output=convert(fixture,target,image_max_dimension=40)
    actual=Image.open(convert(output) if target in ['webp','avif','heic','gif'] else output)
    assert actual.size==(24,40),target

# ICC normalization is independent of orientation.
profile=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
image.save(source,exif=b'Exif\0\0'+RESERVED,icc_profile=profile)
profile_base=encode('profile.heic'); fixture=root/'profile-exif.heic'
attach_exif(module,profile_base,fixture,tiff(7))
expected=Image.open(convert(profile_base)).convert('RGBA').transpose(operations[7])
actual=Image.open(convert(fixture)).convert('RGBA')
assert actual.tobytes()==expected.tobytes(),'ICC+EXIF'

# High-bit-depth HDR is tone-mapped before the same original EXIF transform.
from hdr_reference import chart, write_png16, read_png16, tone_reference, chunk
width,height,codes=chart('pq'); hdr_png=root/'hdr.png'; write_png16(hdr_png,width,height,codes)
data=hdr_png.read_bytes(); hdr_png.write_bytes(data[:33]+chunk(b'eXIf',RESERVED)+data[33:])
hdr=root/'hdr.heic'
result=run(module/'heif-enc.exe','-L','-b','10','-p','chroma=444','-p','x265:pools=1','-p','x265:frame-threads=1',
           '--colour_primaries','9','--transfer_characteristic','16','--matrix_coefficients','0','-o',hdr,hdr_png)
assert result.returncode==0,result.stderr
fixture=root/'hdr-exif.heic'; attach_exif(module,hdr,fixture,tiff(6))
raw=root/'hdr-raw.png'; result=run(module/'heif-dec.exe','--quiet',hdr,raw); assert result.returncode==0,result.stderr
w,h,values=read_png16(raw); expected=tone_reference(w,h,values,'pq').transpose(operations[6])
actual=Image.open(convert(fixture)).convert('RGBA')
assert actual.size==expected.size and max(v[1] for v in ImageChops.difference(actual,expected).getextrema())<=2,'HDR+EXIF'
print('PASS HEIC EXIF: all eight orientations, endian, container precedence/identity, malformed and duplicate EXIF')
