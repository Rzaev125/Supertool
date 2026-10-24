"""Observe the real child protocol while FFmpeg converts and publishes a file."""
import argparse
import hashlib
import queue
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from execution_protocol import execution_event


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def convert(executable, source, output, temporary, extra=(), expect_success=True, require_intermediate=True):
    before = digest(source)
    previous_output = digest(output) if output.exists() else None
    observations = queue.Queue()
    process = subprocess.Popen(
        [str(executable), '--convert', str(source), str(output), str(temporary), *map(str, extra)],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW,
    )

    def read_events():
        try:
            while True:
                line = process.stdout.readline(1026)
                if not line:
                    break
                # Record existence at receipt, before waiting for process exit.
                observations.put((line, output.exists()))
        finally:
            observations.put(None)

    reader = threading.Thread(target=read_events, daemon=True)
    reader.start()
    events = []
    deadline = time.monotonic() + 60
    try:
        while True:
            observation = observations.get(timeout=max(0.01, deadline - time.monotonic()))
            if observation is None:
                break
            raw, exists_at_receipt = observation
            assert len(raw) <= 1024 and raw.endswith(b'\n'), 'unbounded or unterminated protocol event'
            fields = raw[:-1].decode('utf-8', errors='strict').split('\t')
            if fields[0] == 'SWE1':
                execution_event(fields)
                continue
            assert len(fields) == 3 and fields[0] == 'SWP1', f'unexpected stdout: {raw!r}'
            percent = int(fields[1])
            assert -1 <= percent <= 100 and fields[2], 'invalid progress value or empty stage'
            assert all(ord(character) >= 32 for character in fields[2]), 'stage contains control characters'
            assert str(source) not in fields[2] and str(output) not in fields[2], 'stage exposes a source or output path'
            if percent == 100:
                assert exists_at_receipt, '100 percent arrived before publication'
            events.append((percent, fields[2]))
        code = process.wait(timeout=max(0.01, deadline - time.monotonic()))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        reader.join(timeout=5)
        assert not reader.is_alive(), 'stdout reader remained blocked after child exit'
        process.stdout.close()

    assert digest(source) == before, 'original source changed'
    assert not temporary.exists(), 'conversion staging remains after child exit'
    assert events and events[0][0] == -1, 'initial indeterminate progress missing'
    if expect_success:
        detail = Path(str(temporary) + '.error.txt')
        assert code == 0, detail.read_text('utf-8') if detail.exists() else f'child exit {code}'
        assert output.is_file() and output.stat().st_size > 0, 'published output missing or empty'
        assert events[-1][0] == 100, 'final completion missing'
        assert sum(percent == 100 for percent, _ in events) == 1, 'completion reported more than once'
        engine = [percent for percent, stage in events if stage.startswith('Конвертация') and 0 <= percent < 100]
        assert engine, 'FFmpeg did not deliver numeric progress before completion'
        if require_intermediate:
            assert any(0 < percent < 99 for percent in engine), 'FFmpeg did not deliver intermediate numeric progress before completion'
        for stage in {stage for _, stage in events if stage.startswith('Конвертация')}:
            attempt = [percent for percent, event_stage in events if event_stage == stage and 0 <= percent < 100]
            assert attempt == sorted(attempt), 'FFmpeg progress moved backwards within one encoder attempt'
    else:
        assert code != 0, 'failed conversion reported success'
        assert all(percent != 100 for percent, _ in events), 'failed conversion reported 100 percent'
        if previous_output is None:
            assert not output.exists(), 'failed conversion published an output'
        else:
            assert digest(output) == previous_output, 'failed conversion replaced an existing output'
    return events


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    executable = parser.parse_args().exe.resolve()
    ffmpeg = executable.parent / 'modules' / 'ffmpeg' / 'ffmpeg.exe'
    fixture_parent = Path.cwd() / 'test-output'
    fixture_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='media-progress-', dir=fixture_parent) as temporary_root:
        root = Path(temporary_root) / 'Прогресс с пробелами'
        root.mkdir()
        source = root / 'Оригинал.mp4'
        generated = subprocess.run(
            [str(ffmpeg), '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=30:duration=6',
             '-c:v', 'libx264', '-preset', 'ultrafast', '-threads', '1', '-pix_fmt', 'yuv420p', str(source)],
            capture_output=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW,
        )
        assert generated.returncode == 0, generated.stderr.decode('utf-8', errors='replace')
        snapshot = root / 'options.ini'
        snapshot.write_text('version=1\nquality=balanced\nresources=economy\nvideo_acceleration=cpu\n', encoding='utf-8')
        events = convert(executable, source, root / 'Готово.mp4', root / 'work-success', ['mp4', '--options', snapshot])
        assert snapshot.is_file(), 'child deleted caller-owned options'
        # The legacy CLI infers the output format without an options snapshot.
        convert(executable, source, root / 'Legacy.mp4', root / 'work-legacy', require_intermediate=False)
        corrupt = root / 'Повреждён.mp4'
        corrupt.write_bytes(b'not an MP4 container')
        convert(executable, corrupt, root / 'Failed.mp4', root / 'work-failed', ['mp4'], expect_success=False)
        existing = root / 'Existing.mp4'
        existing.write_bytes(b'foreign output must remain intact')
        convert(executable, source, existing, root / 'work-existing', ['mp4'], expect_success=False)
        print(f'PASS real media progress: {len(events)} events, numeric FFmpeg updates, 100 after publication, '
              'legacy CLI, failed/colliding outputs without 100, originals preserved', flush=True)


if __name__ == '__main__':
    main()
