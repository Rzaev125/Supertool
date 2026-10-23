"""Allowlisted photo tags survive, while private metadata and old modes stay isolated."""
import argparse
import hashlib
import subprocess
import struct
import tempfile
from pathlib import Path
from PIL import Image
from heic_fixture import RESERVED, attach_exif

parser=argparse.ArgumentParser(); parser.add_argument('--exe',type=Path,required=True)
exe=parser.parse_args().exe.resolve(); module=exe.parent/'modules/heic'
base=Path.cwd()/'test-output'; base.mkdir(exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='photo-tags-',dir=base)); counter=0

def convert(source,target,mode='photo_details',fail=False):
    global counter
    counter+=1; output=root/f'out-{counter}.{target}'; work=root/f'work-{counter}'
    snapshot=root/f'options-{counter}.ini'; snapshot.write_text(f'version=1\nimage_metadata={mode}\n',encoding='utf-8')
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
print('PASS photo metadata: JPEG/TIFF/HEIC, selected tags and Unicode, privacy, old modes, unsupported target')
