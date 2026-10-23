# Hardware H.264 encoding

HEVC/AV1 support was added in 0.11: see [video-codecs.md](video-codecs.md) for container choices, same-codec fallback and current validation. The H.264 behavior and historical measurements below remain applicable to that codec.

ShiftWheel 0.5.0 can encode MP4, MKV and MOV video using NVIDIA NVENC,
Intel Quick Sync Video (QSV), or AMD AMF. The automatic setting is the
default for new settings and older INI files without the new key:

```ini
video_acceleration=auto
```

Choose **CPU only** in settings, or use `video_acceleration=cpu`, to use
`libx264` without initializing or probing a GPU. Each queued conversion
receives its own immutable settings snapshot.

WebM still uses CPU VP9. Audio conversion is unchanged. Video decoding,
scaling, even-dimension padding and audio encoding remain on the CPU.
The resource setting limits the existing CPU decoder/filter/encoder
thread options; it does not impose a GPU utilization or power limit.

## Selection and fallback

Automatic conversion tries NVIDIA, Intel, then AMD, stopping at the first
successful initialization. Each probe actually encodes three 256×256
frames with the requested quality settings and flushes the encoder. The
probe writes to FFmpeg's null muxer and has a five-second process timeout.
There are at most three probes per H.264 conversion, with no background
GPU polling, cached availability promise, driver installation or service.
Process-tree cleanup can add a short delay after a timeout.

A compiled encoder name alone does not prove that the GPU, driver and
encoder settings work. An unavailable, busy or incompatible device moves
selection to the next candidate; if none initializes, CPU encoding starts.
A successful small probe also cannot guarantee that every source size or
driver state will work during the full conversion.

If the selected GPU fails during conversion, ShiftWheel removes that
attempt's private output directory and retries once using `libx264`.
The CPU retry has a distinct diagnostic log. Its failure is final.
Hardware output is staged separately, so cleanup does not remove the
original, an existing destination, or a destination that appeared during
encoding. Publication still uses an exclusive move without replacement.
Cancellation terminates the worker process tree; it is not a reason to
retry on CPU. An engine's explicit Windows cancellation exit is also
propagated without retry.

The progress stage identifies NVIDIA NVENC, Intel QSV, AMD AMF or CPU.
After a runtime fallback it identifies the CPU retry. Numeric progress
may restart for that retry; encoder progress is capped at 99 percent.
Only successful publication reports 100 percent.

## Quality settings

These are independent encoder controls, not equivalent quality scores.
The same preset can produce different quality, size and speed on different
devices. AAC bitrate, optional audio mapping, scaling and MP4/MOV
fast-start behavior retain their existing settings.

| Encoder | Compact | Balanced | High |
|---|---|---|---|
| CPU `libx264` | `fast`, CRF 28 | `fast`, CRF 23 | `fast`, CRF 18 |
| NVIDIA `h264_nvenc` | `p3`, CQ 28 | `p4`, CQ 23 | `p5`, CQ 18 |
| Intel `h264_qsv` | `fast`, global quality 28 | `medium`, global quality 23 | `slow`, global quality 18 |
| AMD `h264_amf` | `speed`, QP 28 | `balanced`, QP 23 | `quality`, QP 18 |

NVENC uses VBR with `-b:v 0`. AMF uses CQP with the stated QP applied to
I, P and B frames. QSV accepts NV12 input; the resulting H.264 remains
8-bit 4:2:0, as with the other encoders. GPU options were checked against
the bundled FFmpeg 9.0.2 `-h encoder=h264_nvenc`, `h264_qsv` and `h264_amf`
help, and the primary references below:

- [NVIDIA FFmpeg integration and encoder presets](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)
- [FFmpeg QSV rate control and preset documentation](https://ffmpeg.org/ffmpeg-codecs.html#QSV-Encoders)
- [AMD AMF encoder settings and tuning](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/AMF-Encoder-Settings-and-Tuning-in-FFmpeg)

## Verification on 2026-10-22

The local tests used the packaged Windows x64 FFmpeg
`9.0.2-essentials_build-www.gyan.dev`:

| Device | Installed Windows driver | Observed result |
|---|---|---|
| NVIDIA GeForce GTX 1660 SUPER | 32.0.16.1714 | Real initialization and full NVENC conversions passed for all three quality presets. Automatic selection chose NVENC. |
| Intel UHD Graphics 770 | 32.0.101.7088 | Real initialization and full QSV conversions passed for all three presets when the test harness excluded the NVIDIA probe. |
| AMD | No AMD GPU available | Actual AMF initialization failed because `amfrt64.dll` was unavailable; conversion fell back to CPU. Successful encoding on AMD hardware remains unverified. |

The GTX 1660 SUPER rejected a real 128×128 input after the 256×256 probe
succeeded, reporting a minimum frame-dimension restriction. The application
then produced a valid 128×128 H.264 result on CPU. This exercises a genuine
runtime fallback in addition to controlled failure tests; it is not a
general claim about the minimum size supported by every NVIDIA GPU.

`media_gpu_policy` checks deterministic encoder selection, bounded probes,
CPU-only behavior, unrelated formats, encoder-specific flags, cancellation,
one CPU retry, separate logs and ownership-aware cleanup. Filesystem effects
are real; only the external encoder process boundary is substituted.
`engine_progress` verifies that engine progress retains its selected stage
and never reports publication completion.

`media_gpu_real` runs the production media pipeline against real FFmpeg.
It checks MP4/MKV/MOV, all presets, 641×361 input scaled/padded to 426×240,
AAC audio, strict decoding of complete output, CPU-only conversion,
unavailable-device fallback, runtime failure after real hardware output,
the 128×128 driver limitation, progress, source integrity and existing
destination preservation. Intel and AMD path isolation exists only in the
test harness; the application exposes Auto and CPU settings.

The six targeted CTest tests passed together: `media_gpu_policy`,
`engine_progress`, `conversion_media`, `conversion_presets`,
`media_progress`, and `media_gpu_real`. The real GPU matrix completed in
12.35 seconds and the six-test run in 29.86 seconds on this machine.
These are test durations, not a performance comparison or speed guarantee.

Run the hardware tests from a configured build with its modules present:

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(media_gpu_policy|media_gpu_real|engine_progress|media_progress)$'
```

The real matrix reports which backend actually ran. On a machine without
working hardware encoders, CPU fallback can pass while hardware success
remains unverified there.
