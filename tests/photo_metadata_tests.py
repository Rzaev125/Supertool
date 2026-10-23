"""Allowlisted photo tags survive, while private metadata and old modes stay isolated."""
import argparse
import hashlib
import subprocess
import struct
import tempfile
from pathlib import Path
from PIL import Image, PngImagePlugin
from heic_fixture import RESERVED, attach_exif
from photo_metadata_fixtures import (XMP, XMP_TOKEN, THUMB_TOKEN, STALE_SIZE, append_thumbnail, assert_clean,
                                    assert_fixture_tiff, bmff_metadata,
                                    contaminated_copy, jpeg_segment,
                                    native_jpeg_fixture, native_tiff_fixture, xmp_png)

parser=argparse.ArgumentParser(); parser.add_argument('--exe',type=Path,required=True)
exe=parser.parse_args().exe.resolve(); module=exe.parent/'modules/heic'
base=Path.cwd()/'test-output'; base.mkdir(exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='photo-tags-',dir=base)); counter=0

def convert(source,target,mode='photo_details',fail=False,maximum=0):
    global counter
    counter+=1; output=root/f'out-{counter}.{target}'; work=root/f'work-{counter}'
    snapshot=root/f'options-{counter}.ini'; snapshot.write_text(f'version=1\nimage_metadata={mode}\nimage_max_dimension={maximum}\n',encoding='utf-8')
    before=hashlib.sha256(source.read_bytes()).digest()
    result=subprocess.run(list(map(str,[exe,'--convert',source,output,work,target,'--options',snapshot])),capture_output=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
    assert hashlib.sha256(source.read_bytes()).digest()==before and not work.exists()
    error=Path(str(work)+'.error.txt')
    if fail:
        assert result.returncode!=0 and not output.exists(),'malformed metadata accepted'
        return
    assert result.returncode==0 and output.exists(),error.read_text('utf-8') if error.exists() else result.stderr
    return output

author='Əli Рзаев'; xp=(author+'\0').encode('utf-16le')
tags=Image.Exif(); tags[315]='Artist'; tags[33432]='Copyright 2026'; tags[40093]=xp
tags[274]=1; tags[34665]={36867:'2026:10:23 12:34:56',37500:b'SECRET-MAKER',42033:'SECRET-SERIAL'}
tags[34853]={1:'N',2:(40.,30.,0.)}; tags[270]='PRIVATE-DESCRIPTION'
image=Image.new('RGB',(80,48),(180,40,90))
sources=[]
for extension in ['jpg','tiff']:
    source=root/f'input.{extension}'; image.save(source,exif=tags.tobytes()); sources.append(source)
reserved=root/'reserved.png'; image.save(reserved,exif=b'Exif\0\0'+RESERVED)
encoded=root/'encoded.heic'
result=subprocess.run(list(map(str,[module/'heif-enc.exe','-L','-p','chroma=444','--colour_primaries','1','--transfer_characteristic','13','-o',encoded,reserved])),capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
assert result.returncode==0,result.stderr
heic=root/'input.heic'; attach_exif(module,encoded,heic,tags.tobytes()[6:]); sources.append(heic)
for source in sources:
    for target in ['jpg','tiff']:
        output=convert(source,target); metadata=Image.open(output).getexif()
        assert metadata.get(315)=='Artist' and metadata.get(33432)=='Copyright 2026',dict(metadata)
        assert bytes(metadata[40093])==xp,'Unicode author changed'
        nested=metadata.get_ifd(34665)
        assert nested.get(36867)=='2026:10:23 12:34:56',nested
        assert not ({34853,270,274,700} & metadata.keys()),dict(metadata)
        assert not ({37500,42033} & nested.keys()),nested
    for mode in ['remove','capture_date']:
        metadata=Image.open(convert(source,'jpg',mode)).getexif()
        assert not ({315,33432,40093,34853,270} & metadata.keys())
        nested=metadata.get_ifd(34665)
        assert (36867 in nested)==(mode=='capture_date' and source.suffix!='.heic')
    metadata=Image.open(convert(source,'png')).getexif()
    assert not ({315,33432,40093,34853,34665} & metadata.keys())

def root_tiff(entries):
    result=bytearray(b'II'+struct.pack('<HIH',42,8,len(entries))+b'\0'*(12*len(entries)+4))
    for i,(tag,kind,value,count) in enumerate(entries):
        at=10+12*i
        result[at:at+8]=struct.pack('<HHI',tag,kind,count if count is not None else len(value))
        if len(value)<=4: result[at+8:at+12]=value.ljust(4,b'\0')
        else:
            result[at+8:at+12]=struct.pack('<I',len(result)); result.extend(value)
    return bytes(result)

# These tags are read from the ORIGINAL bytes, before WIC coalesces duplicates.
cases=[
    [(315,2,b'First\0',None),(315,2,b'Other\0',None)],
    [(315,2,b'Bad\0inside\0',None)],
    [(315,2,b'No terminator',None)],
    [(315,1,b'Wrong type\0',None)],
    [(315,2,b'X\0',0xffffffff)],
    [(40093,1,b'\0\xd8\0\0',None)],
]
for index,entries in enumerate(cases):
    payload=root_tiff(entries+[(33432,2,b'Valid copyright\0',None)])
    fixture=root/f'invalid-optional-{index}.heic'; attach_exif(module,encoded,fixture,payload)
    for target in ['jpg','tiff']:
        result=Image.open(convert(fixture,target)).getexif()
        assert 315 not in result and 40093 not in result and result[33432]=='Valid copyright',dict(result)
payload=bytearray(root_tiff([(315,2,b'Author\0',None)]));payload[18:22]=struct.pack('<I',0xfffffff0)
fixture=root/'invalid-offset.heic'; attach_exif(module,encoded,fixture,payload);convert(fixture,'jpg',fail=True)

# The ASCII EXIF fields retain bytes; XPAuthor carries portable Unicode text.
payload=root_tiff([(33432,2,b'\xc2\xa9 \xc4\xb0lham\0',None)])
fixture=root/'byte-encoding.heic';attach_exif(module,encoded,fixture,payload)
for target in ['jpg','tiff']:
    result=Image.open(convert(fixture,target)).getexif()
    assert result[33432].encode('latin-1')==b'\xc2\xa9 \xc4\xb0lham','byte encoding changed'

# Populated XMP + genuine JPEG thumbnails + stale EXIF dimensions. Check the
# fixtures BEFORE conversion, so an encoder silently dropping a payload fails.
rich = Image.Exif(); rich.load(tags.tobytes())
rich[700] = XMP
nested = rich.get_ifd(34665).copy(); nested[40962], nested[40963] = STALE_SIZE
rich[34665] = nested
jpeg_source = root/'rich.jpg'; rich_exif = native_jpeg_fixture(image,rich,jpeg_source)
tiff_source = root/'rich.tiff'; native_tiff_fixture(image,rich,tiff_source)
heic_png = root/'rich.png'; xmp_png(image,rich_exif,heic_png)
heic_source = root/'rich.heic'
result=subprocess.run(list(map(str,[module/'heif-enc.exe','-L','-p','chroma=444','-p','x265:pools=1',
    '--colour_primaries','1','--transfer_characteristic','13','-o',heic_source,heic_png])),
    capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
assert result.returncode==0,result.stderr
heic_bytes=heic_source.read_bytes(); items,_=bmff_metadata(heic_bytes)
assert any(tag==b'Exif' for tag,_ in items) and rich_exif in heic_bytes,'HEIC fixture lost original thumbnail/EXIF'
assert any(tag==b'mime' and b'application/rdf+xml\0' in payload for tag,payload in items),'HEIC fixture lost native XMP item'
assert XMP_TOKEN in heic_bytes
assert_fixture_tiff(rich_exif)

matrix_outputs=[]
for source in [jpeg_source,tiff_source,heic_source]:
    for mode in ['remove','capture_date','photo_details']:
        for target in ['jpg','tiff','png','bmp','webp','avif','heic','gif']:
            output=convert(source,target,mode,maximum=40)
            allow_photo=target in ['jpg','tiff'] and (mode=='photo_details' or (mode=='capture_date' and source.suffix!='.heic'))
            assert_clean(output,allow_photo=allow_photo)
            if target in ['jpg','tiff']:
                with Image.open(output) as decoded:
                    actual=decoded.getexif(); nested=actual.get_ifd(34665)
                    assert (nested.get(36867)=='2026:10:23 12:34:56')==allow_photo
                    if mode=='photo_details':
                        assert actual.get(315)=='Artist' and actual.get(33432)=='Copyright 2026' and bytes(actual[40093])==xp
                    else:
                        assert not ({315,33432,40093}&actual.keys())
            matrix_outputs.append(output)

# Prove that the oracle fails on valid files with deliberately retained native
# metadata. These contain NO sentinel tokens, so byte scanning alone cannot pass.
negative_controls=0
def reject_contaminated(path, expected):
    global negative_controls
    data=path.read_bytes()
    assert XMP_TOKEN not in data and THUMB_TOKEN not in data,'negative control must exercise structural checks'
    if path.suffix not in ['.avif','.heic']:
        with Image.open(path) as decoded:
            decoded.load(); assert decoded.size==(40,24),'negative control corrupted pixels/geometry'
    try:
        assert_clean(path)
    except AssertionError as error:
        assert expected in str(error),f'negative control rejected for wrong reason: {error}'
        negative_controls+=1
    else:
        raise AssertionError(f'privacy oracle accepted retained metadata: {path.name}')

reasons={'.jpg':'JPEG XMP copied','.tiff':'private EXIF/thumbnail copied',
         '.png':'PNG metadata chunk copied','.bmp':'BMP color-profile payload retained',
         '.webp':'WebP metadata chunk copied','.gif':'GIF metadata application copied'}
for clean in matrix_outputs[:8]:
    if clean.suffix in ['.avif','.heic']:
        continue
    contaminated=root/('contaminated'+clean.suffix)
    contaminated_copy(clean,contaminated)
    reject_contaminated(contaminated,reasons[clean.suffix])

ordinary_xmp=root/'ordinary.xmp'; ordinary_xmp.write_bytes(XMP.replace(XMP_TOKEN,b'ordinary-private-description'))
clean_png=matrix_outputs[2]
avif=root/'contaminated.avif'
result=subprocess.run(list(map(str,[exe.parent/'modules/avif/avifenc.exe','-j','1','-s','8','--xmp',ordinary_xmp,clean_png,avif])),
    capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
assert result.returncode==0,result.stderr
items,_=bmff_metadata(avif.read_bytes()); assert any(tag==b'mime' for tag,_ in items)
reject_contaminated(avif,'metadata BMFF item copied')
with Image.open(clean_png) as small:
    info=PngImagePlugin.PngInfo()
    info.add_itxt('XML:com.adobe.xmp',ordinary_xmp.read_text('utf-8'))
    contaminated_png=root/'ordinary.png'; small.save(contaminated_png,pnginfo=info)
heic=root/'contaminated.heic'
result=subprocess.run(list(map(str,[module/'heif-enc.exe','-L','-p','chroma=444','-p','x265:pools=1',
    '--colour_primaries','1','--transfer_characteristic','13','-o',heic,contaminated_png])),
    capture_output=True,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
assert result.returncode==0,result.stderr
items,_=bmff_metadata(heic.read_bytes()); assert any(tag==b'mime' for tag,_ in items)
reject_contaminated(heic,'metadata BMFF item copied')

# Isolate the thumbnail and stale-size checks from XMP and sentinel checks.
clean_jpeg=matrix_outputs[0].read_bytes()
thumb_tags=Image.Exif()
thumbnail_only=append_thumbnail(thumb_tags.tobytes()[6:]).replace(THUMB_TOKEN,b'T'*len(THUMB_TOKEN))
bad_thumbnail=root/'thumbnail-only.jpg'
bad_thumbnail.write_bytes(clean_jpeg[:2]+jpeg_segment(0xe1,b'Exif\0\0'+thumbnail_only)+clean_jpeg[2:])
reject_contaminated(bad_thumbnail,'private EXIF/thumbnail copied')
size_tags=Image.Exif();size_tags[34665]={40962:STALE_SIZE[0],40963:STALE_SIZE[1]}
bad_dimensions=root/'stale-size-only.jpg'
bad_dimensions.write_bytes(clean_jpeg[:2]+jpeg_segment(0xe1,size_tags.tobytes())+clean_jpeg[2:])
reject_contaminated(bad_dimensions,'stale EXIF size copied')
assert negative_controls==10
print(f'PASS photo metadata: selected tags/Unicode/privacy; {len(matrix_outputs)} native output checks, {negative_controls} rejected metadata leaks (XMP/thumbnail/stale size/profile)')
