"""Attach original EXIF after encoding, without translating it into HEIF transforms."""
import struct
import ctypes as C
import os
RESERVED = b'II' + struct.pack('<HIHHH',42,8,1,274,3) + struct.pack('<IH',1,1)+b'\0'*6 + b'SHIFTWHEEL-EXIF-RESERVE-'*180


def attach_exif(module, source, output, *blocks):
    assert len(blocks)==1
    data=source.read_bytes()
    assert data.count(RESERVED)==1, 'encoder did not preserve reserved EXIF'
    block=blocks[0]; assert len(block)<=len(RESERVED)
    output.write_bytes(data.replace(RESERVED,block+b'\0'*(len(RESERVED)-len(block))))


class HeifError(C.Structure):
    _fields_ = [('code', C.c_int), ('subcode', C.c_int), ('message', C.c_char_p)]


def encode_exif_blocks(module, source, output, blocks):
    """Decode a small seed into a NEW writable context, then attach arbitrary EXIF.

    libheif1.23.6 cannot write a context read from a file. Re-encoding a tiny
    fixture in a fresh context allows genuine independent metadata items, with
    no byte-size/offset patching or huge PNG metadata limits in the fixture path.
    Returns the original stored blocks read back through a fresh context.
    """
    with os.add_dll_directory(str(module)):
        lib = C.CDLL(str(module/'libheif.dll'))
        pointer = C.c_void_p; out = C.POINTER(pointer)
        signatures = {
            'heif_get_version_number': (C.c_uint32, []),
            'heif_init': (HeifError, [pointer]), 'heif_deinit': (None, []),
            'heif_context_alloc': (pointer, []), 'heif_context_free': (None, [pointer]),
            'heif_context_read_from_file': (HeifError, [pointer,C.c_char_p,pointer]),
            'heif_context_get_primary_image_handle': (HeifError, [pointer,out]),
            'heif_image_handle_release': (None, [pointer]),
            'heif_decode_image': (HeifError, [pointer,out,C.c_int,C.c_int,pointer]),
            'heif_image_release': (None, [pointer]),
            'heif_image_set_raw_color_profile': (HeifError, [pointer,C.c_char_p,pointer,C.c_size_t]),
            'heif_context_get_encoder_for_format': (HeifError, [pointer,C.c_int,out]),
            'heif_encoder_release': (None, [pointer]),
            'heif_encoder_set_lossless': (HeifError, [pointer,C.c_int]),
            'heif_encoder_set_parameter': (HeifError, [pointer,C.c_char_p,C.c_char_p]),
            'heif_context_encode_image': (HeifError, [pointer,pointer,pointer,pointer,out]),
            'heif_context_add_exif_metadata': (HeifError, [pointer,pointer,pointer,C.c_int]),
            'heif_context_write_to_file': (HeifError, [pointer,C.c_char_p]),
            'heif_image_handle_get_number_of_metadata_blocks': (C.c_int, [pointer,C.c_char_p]),
            'heif_image_handle_get_list_of_metadata_block_IDs': (C.c_int, [pointer,C.c_char_p,C.POINTER(C.c_uint32),C.c_int]),
            'heif_image_handle_get_metadata_size': (C.c_size_t, [pointer,C.c_uint32]),
            'heif_image_handle_get_metadata': (HeifError, [pointer,C.c_uint32,pointer]),
        }
        for name,(result,arguments) in signatures.items():
            function=getattr(lib,name);function.restype=result;function.argtypes=arguments
        def check(error):
            assert error.code==0, f'libheif fixture: {error.code}/{error.subcode}: {error.message!r}'
        assert lib.heif_get_version_number()==0x01170600
        check(lib.heif_init(None))
        contexts=[]; handles=[]; pixels=pointer(); encoder=pointer()
        def context():
            value=lib.heif_context_alloc();assert value;contexts.append(value);return value
        def primary(ctx):
            handle=pointer();check(lib.heif_context_get_primary_image_handle(ctx,C.byref(handle)))
            handles.append(handle);return handle
        try:
            read=context();check(lib.heif_context_read_from_file(read,str(source).encode('utf-8'),None))
            handle=primary(read)
            check(lib.heif_decode_image(handle,C.byref(pixels),1,11,None))  # RGB/interleaved RGBA
            # The generic encoder's default output NCLX is not our supported
            # sRGB transfer13. Keep an explicit sRGB ICC on these SDR fixtures.
            from PIL import ImageCms
            profile=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
            buffer=C.create_string_buffer(profile)
            check(lib.heif_image_set_raw_color_profile(pixels,b'prof',buffer,len(profile)))
            write=context()
            check(lib.heif_context_get_encoder_for_format(write,1,C.byref(encoder)))  # HEVC
            check(lib.heif_encoder_set_lossless(encoder,1))
            for name,value in [(b'chroma',b'444'),(b'x265:pools',b'1'),(b'x265:frame-threads',b'1')]:
                check(lib.heif_encoder_set_parameter(encoder,name,value))
            encoded=pointer();check(lib.heif_context_encode_image(write,pixels,encoder,None,C.byref(encoded)))
            handles.append(encoded)
            for payload in blocks:
                buffer=C.create_string_buffer(payload)
                check(lib.heif_context_add_exif_metadata(write,encoded,buffer,len(payload)))
            check(lib.heif_context_write_to_file(write,str(output).encode('utf-8')))
            verify=context();check(lib.heif_context_read_from_file(verify,str(output).encode('utf-8'),None))
            handle=primary(verify)
            count=lib.heif_image_handle_get_number_of_metadata_blocks(handle,b'Exif')
            assert count==len(blocks),'fixture changed EXIF item count'
            ids=(C.c_uint32*count)()
            assert lib.heif_image_handle_get_list_of_metadata_block_IDs(handle,b'Exif',ids,count)==count
            stored=[]
            for item in ids:
                length=lib.heif_image_handle_get_metadata_size(handle,item)
                assert 4<=length<=8*1024*1024,'unexpected fixture metadata size'
                buffer=C.create_string_buffer(length);check(lib.heif_image_handle_get_metadata(handle,item,buffer))
                stored.append(buffer.raw)
            return stored
        finally:
            for handle in reversed(handles):lib.heif_image_handle_release(handle)
            if encoder:lib.heif_encoder_release(encoder)
            if pixels:lib.heif_image_release(pixels)
            for ctx in reversed(contexts):lib.heif_context_free(ctx)
            lib.heif_deinit()
