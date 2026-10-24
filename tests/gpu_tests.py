"""Real FFmpeg GPU/CPU conversions through production media policy and CLI."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from execution_protocol import execution_event, execution_events


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def run(command, timeout=45):
    result = subprocess.run(list(map(str, command)), capture_output=True, timeout=timeout,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    return result


def events(raw):
    result = []
    for line in raw.decode('utf-8', errors='strict').splitlines():
        fields = line.split('\t')
        if fields[0] == 'SWE1':
            execution_event(fields)
            continue
        assert len(fields) == 3 and fields[0] == 'SWP1', f'Unexpected child output: {line!r}'
        result.append((int(fields[1]), fields[2]))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--harness', required=True, type=Path)
    args = parser.parse_args()
    exe, harness = args.exe.resolve(), args.harness.resolve()
    ffmpeg = exe.parent / 'modules' / 'ffmpeg' / 'ffmpeg.exe'
    ffprobe = ffmpeg.with_name('ffprobe.exe')
    fixture_parent = Path.cwd() / 'test-output'
    fixture_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='gpu-real-', dir=fixture_parent) as temporary:
        root = Path(temporary).resolve() / 'GPU ə и пробелы'
        root.mkdir()
        source = root / 'Оригинал.mkv'
        run([ffmpeg, '-hide_banner', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc=size=641x361:rate=30:duration=2',
             '-f', 'lavfi', '-i', 'sine=frequency=700:sample_rate=48000:duration=2', '-c:v', 'ffv1',
             '-pix_fmt', 'yuv444p', '-c:a', 'pcm_s16le', '-threads', '1', source])
        original = digest(source)
        number = 0

        def inspect(output, expected_video='h264', dimensions=(426, 240), audio='aac'):
            data = json.loads(run([ffprobe, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', output]).stdout)
            video = next(stream for stream in data['streams'] if stream['codec_type'] == 'video')
            assert video['codec_name'] == expected_video and (video['width'], video['height']) == dimensions, video
            assert video['pix_fmt'] == 'yuv420p', video
            sound = next(stream for stream in data['streams'] if stream['codec_type'] == 'audio')
            assert sound['codec_name'] == audio, sound
            assert 1.9 <= float(data['format']['duration']) <= 2.2, data['format']
            # Decode every packet with strict error handling, beyond metadata checks.
            run([ffmpeg, '-hide_banner', '-v', 'error', '-xerror', '-err_detect', 'explode', '-i', output, '-f', 'null', '-'])

        def harness_convert(mode='auto', quality='balanced', container='mp4', cpu=False, fixture=source, scale=240):
            nonlocal number
            number += 1
            work = root / f'work-{number}'
            output = work / f'result.{container}'
            snapshot = root / f'options-{number}.ini'
            snapshot.write_text(f'version=1\nquality={quality}\nresources=economy\nvideo_acceleration={"cpu" if cpu else "auto"}\n'
                                f'video_max_height={scale}\naudio_bitrate_kbps=160\n', encoding='utf-8')
            result = run([harness, '--real-media', fixture, output, work, snapshot, mode])
            observed = events(result.stdout)
            assert observed and all(-1 <= percent < 100 for percent, _ in observed), 'internal encoder must not report publication'
            trace = [line.split('\t') for line in (work / 'calls.tsv').read_text('utf-8').splitlines()]
            encodes = [line[1] for line in trace if line[0] == 'encode']
            probes = [line[1] for line in trace if line[0] == 'probe']
            actual = [fields for fields in execution_events(result.stdout) if fields[1] == 'encoder']
            assert len(actual) == 1 and actual[0][2] == encodes[-1], 'reported encoder was not the successful final attempt'
            assert (actual[0][4] == '1') == (len(encodes) > 1), 'runtime fallback fact differs from actual calls'
            assert digest(source) == original, 'original media changed'
            assert output.is_file() and output.stat().st_size > 0, 'converted media missing'
            assert not (work / 'media-hardware').exists(), 'owned hardware attempt was not cleaned'
            for encoder, label in [('h264_nvenc', 'NVIDIA'), ('h264_qsv', 'Intel'), ('h264_amf', 'AMD'), ('libx264', 'CPU')]:
                if encoder in encodes:
                    assert any(stage.startswith('Конвертация:') and label in stage for _, stage in observed), f'missing {label} progress stage'
            return output, encodes, probes, work, observed

        # All H.264 containers use real automatic initialization and preserve
        # optional audio, odd input dimensions, scaling and decodable 4:2:0.
        selected = None
        for container in ('mp4', 'mkv', 'mov'):
            output, encodes, probes, _, _ = harness_convert(container=container)
            inspect(output)
            assert probes and probes[0] == 'h264_nvenc', 'auto did not probe real NVENC initialization'
            selected = encodes[-1]
        for quality in ('compact', 'high'):
            output, _, _, _, _ = harness_convert(quality=quality)
            inspect(output)

        # NVIDIA cannot hide problems in the Intel settings: exclude only its
        # probe at the process boundary, then initialize/encode using real QSV.
        intel = None
        for quality in ('compact', 'balanced', 'high'):
            output, encodes, probes, _, _ = harness_convert(mode='intel', quality=quality)
            inspect(output)
            assert probes[:2] == ['h264_nvenc', 'h264_qsv'], probes
            intel = encodes[-1]
        output, amd_encodes, amd_probes, _, _ = harness_convert(mode='amd')
        inspect(output)
        assert amd_probes == ['h264_nvenc', 'h264_qsv', 'h264_amf'], amd_probes

        output, encodes, probes, _, _ = harness_convert(cpu=True)
        inspect(output)
        assert probes == [] and encodes == ['libx264'], 'CPU-only touched a hardware encoder'
        output, encodes, probes, _, _ = harness_convert(mode='unavailable')
        inspect(output)
        assert len(probes) == 3 and encodes == ['libx264'], 'unavailable hardware did not fall back to actual CPU encoding'

        output, encodes, _, work, observed = harness_convert(mode='runtime-failure')
        inspect(output)
        if selected != 'libx264':
            assert len(encodes) == 2 and encodes[-1] == 'libx264', 'failed real GPU output did not retry CPU exactly once'
            assert (work / f'encode-{encodes[0]}.log').is_file() and (work / 'encode-libx264.log').is_file(), 'fallback logs collided'
            assert any('CPU (после GPU)' in stage for _, stage in observed), 'CPU retry stage missing'

        # On Turing, a successful 256x256 initialization probe cannot encode
        # 128x128 H.264. Exercise that genuine runtime limitation, with no
        # injected error, and confirm the production retry makes a usable file.
        tiny = root / 'Маленький.mkv'
        run([ffmpeg, '-hide_banner', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=128x128:rate=30:duration=2',
             '-f', 'lavfi', '-i', 'sine=duration=2', '-c:v', 'ffv1', '-c:a', 'pcm_s16le', '-threads', '1', tiny])
        tiny_before = digest(tiny)
        output, tiny_encodes, _, _, _ = harness_convert(fixture=tiny, scale=0)
        inspect(output, dimensions=(128, 128))
        assert digest(tiny) == tiny_before, 'tiny original changed'

        # Verify the application publishes a valid result with completion only
        # afterward, and rejects a foreign destination without replacing it.
        published = root / 'Готовый.mp4'
        cli_work = root / 'cli-work'
        cli = run([exe, '--convert', source, published, cli_work, 'mp4'])
        assert events(cli.stdout)[-1][0] == 100 and not cli_work.exists(), 'CLI did not finish and clean staging'
        before = digest(published)
        rejected = subprocess.run([str(exe), '--convert', str(source), str(published), str(root / 'collision'), 'mp4'],
                                  capture_output=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        assert rejected.returncode != 0 and digest(published) == before, 'foreign output was overwritten'
        assert all(percent < 100 for percent, _ in events(rejected.stdout)), 'collision reported success'
        assert digest(source) == original, 'application changed original source'
        print(f'PASS real GPU media: auto={selected}, Intel path={intel}, AMD path={amd_encodes[-1]}, '
              f'tiny real runtime encoders={tiny_encodes}; all presets, MP4/MKV/MOV, scaling, AAC, full decode, '
              'CPU-only, unavailable/runtime fallback, progress and source/foreign-output preservation', flush=True)


if __name__ == '__main__':
    main()
