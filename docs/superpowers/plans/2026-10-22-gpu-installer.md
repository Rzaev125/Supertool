# Hardware video and Windows installer implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development. Root owns shared settings/UI and final integration; independently owned GPU and installer tasks may run in parallel.

**Goal:** Deliver Supertool 0.5.0 with hardware H.264 encoding and a per-user Windows installer.

**Architecture:** Preserve native C++20 Win32, sequential jobs and portable engines. Probe real encoder initialization in conversion workers, prefer available NVIDIA NVENC / Intel QSV / AMD AMF, and fall back to CPU when automatic acceleration cannot run. Package the same verified portable payload using Inno Setup, without system-wide runtimes or services.

**Spec:** User explicitly requests continuing the approved roadmap with hardware video acceleration and a Windows installer. Existing requirements: responsiveness, low idle load, local conversion, source preservation and settings persistence. The choices below are implementation decisions within that authorization.

## Contracts and decisions

- Append `enum class VideoAcceleration { Auto, Cpu };` and `ConversionOptions::video_acceleration=VideoAcceleration::Auto`. INI/snapshot key `video_acceleration=auto|cpu`. Missing key means Auto; invalid values fail before starting a worker. UI offers automatic GPU/CPU and CPU-only.
- Hardware scope: current H.264 output in MP4/MKV/MOV, preserving AAC, mapping, scaling/padding, progress and exclusive publication. WebM remains VP9 on CPU; audio unaffected. Decode and filters remain CPU in this release for portability and consistent filtering.
- Probe actual short encode, not merely compiled encoder names. Bound startup attempts/time, no idle polling or persistent service. Quality presets map explicitly per encoder; encoding quality is not identical between GPUs/CPU. Resource mode still constrains CPU thread options, not GPU utilization.
- Auto must retry safely on CPU after hardware failure: remove only owned partial output, distinct logs, no premature 100%, no source/foreign output modification. Cancellation remains process-tree termination, never a fallback trigger after cancellation.
- GPU agent owns `src/converter.cpp`, new media/GPU modules and tests, and necessary `engines.*` progress stage extension. Root owns options/panel/CMake/version/package/docs. Agent notifies root of exact new source/test targets.
- Installer agent owns `installer/`, `scripts/build-installer.ps1`, optional pinned tool fetch script, installer tests and `docs/installer.md`. Stable AppId; per-user local application directory; full modules; Start menu shortcut, optional desktop shortcut; no mandatory autorun, associations, driver or system installs. Preserve user INI on upgrade/uninstall. Closing running conversions must not happen silently.
- Test installation/upgrade/uninstall in a dedicated workspace directory with registration/shortcuts disabled; normal registry integration must be reviewed separately. Do not install into the user's actual applications directory merely to test. Installer download/compiler tools stay inside workspace; artifact remains unsigned unless a legitimate signing certificate is provided.

## Tasks

- [x] Root: RED settings/snapshot/UI tests; implement validated acceleration setting with immutable per-job snapshot and old-INI compatibility.
- [x] GPU: tests for supported encoder choice, failed initialization, runtime failure/partial cleanup/CPU fallback, CPU-only, unrelated formats and progress; real FFmpeg conversions with available hardware and CPU fallback. Record unavailable hardware honestly.
- [x] Installer: trusted compiler provenance, reproducible build of exact payload; fresh install, upgrade retaining settings, launch and uninstall preserving user-created files; reject active instance/unsafe destinations without forced process killing.
- [x] Integrate 0.5.0, independent review, build/full CTest, native UI check, portable and installer integrity/hash, preserve prior releases/settings, launch updated app.

## Completion evidence

- Release EXE: 674816 bytes, PE ProductVersion 0.5.0; SHA-256 `FF3ABF1655FE728A81C3BB6D05D4DB27A1F8B63FD9366465549EB5F95A1196FB`.
- All 26 CTest checks have passing results: full run 25/26 in 268.76s, then corrected installer-payload check 1/1 in 1.15s. The failure was PowerShell module lookup through a native launcher; no application code changed after the full run. Both logs are retained in build/release-0.5.0-*.log.
- Real NVENC and QSV encode; unavailable AMF and genuine NVENC runtime failure fall back to CPU. AMD hardware success remains unverified.
- Full isolated installer: 19512 installed hashes verified, actual application launch and media conversion, upgrade and payload removal with user files preserved. The asynchronous uninstaller self-cleanup test was corrected and verified twice with the small fixture; details are recorded in docs/verification.md.
- Independent source/feature review has no remaining product findings. Real-profile registration/shortcuts are reviewed statically, not installed into the user's normal application directory.
- Native settings window inspected after launching out/Supertool/Supertool.exe: Auto and CPU-only choices visible, original INI unchanged. Evidence: build/release-0.5.0-ui-check.json. Updated app left running.
- ZIP: 629233622 bytes, 21175 unique entries, full CRC/module/EXE/docs checks passed; SHA-256 `478A79D833A2935C05F606D0703A7F871DC9700801A089DDD8EDBFF7A6CE02E9`.
- Setup: 555961261 bytes, PE version 0.5.0, unsigned; SHA-256 `43C5B857E1356850F6B5CE75D13A3765D1A89C0705F223B6BF37E76D9BEA9F41`. Sidecar digest and payload EXE match. Personal INI excluded from both distributions; prior release retained.

## Review focus

Hardware present but driver/codec unavailable; software fallback after partial file creation; cancellation during probe/encode; installed settings persistence and safe uninstall; installer interactions with an already running conversion.

No existing graphify graph, AGENTS.md, or relevant memory was found. Repository remains an unborn Git workspace; no commit/worktree operations needed. Previous verification and releases must remain available.
