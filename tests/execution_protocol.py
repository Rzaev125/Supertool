"""Validate the additive worker execution protocol in real-process tests."""
STAGES = {'wic', 'libavif', 'libheif', 'libwebp', 'ffmpeg', 'libreoffice', 'poppler', 'pdfium', 'tesseract'}
ENCODERS = {'libx264', 'libx265', 'libaom-av1', 'libvpx-vp9', 'libmp3lame', 'pcm_s16le', 'flac', 'aac', 'libvorbis', 'libopus'} | {f'{c}_{b}' for c in ('h264', 'hevc', 'av1') for b in ('nvenc', 'qsv', 'amf')}


def execution_event(fields):
    assert fields[0] == 'SWE1'
    if fields[1] == 'stage':
        assert len(fields) == 3 and fields[2] in STAGES
    elif fields[1] == 'bytes':
        assert len(fields) == 3 and fields[2].isascii() and fields[2].isdigit() and int(fields[2]) < 2**64
    else:
        assert len(fields) == 5 and fields[1] == 'encoder' and fields[2] in ENCODERS and fields[4] in ('0', '1')
        backend = 'nvidia' if fields[2].endswith('_nvenc') else 'intel' if fields[2].endswith('_qsv') else 'amd' if fields[2].endswith('_amf') else 'cpu'
        assert fields[3] == backend


def execution_events(raw):
    result = []
    for line in raw.decode('utf-8', errors='strict').splitlines():
        if line.startswith('SWE1\t'):
            fields = line.split('\t')
            execution_event(fields)
            result.append(fields)
    return result
