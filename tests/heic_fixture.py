"""Attach original EXIF after encoding, without translating it into HEIF transforms."""
import struct
RESERVED = b'II' + struct.pack('<HIHHH',42,8,1,274,3) + struct.pack('<IH',1,1)+b'\0'*6 + b'SHIFTWHEEL-EXIF-RESERVE-'*180


def attach_exif(module, source, output, *blocks):
    assert len(blocks)==1
    data=source.read_bytes()
    assert data.count(RESERVED)==1, 'encoder did not preserve reserved EXIF'
    block=blocks[0]; assert len(block)<=len(RESERVED)
    output.write_bytes(data.replace(RESERVED,block+b'\0'*(len(RESERVED)-len(block))))
