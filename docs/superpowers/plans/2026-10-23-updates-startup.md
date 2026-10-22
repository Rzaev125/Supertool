# Updates and Windows startup

User authorized continuation of the roadmap. Implement inline using executing-plans.

- [x] Strict HTTPS release manifest (version/url/size/SHA256), bounded streaming download and hash verification; background checks, no UI blocking.
- [x] Installed copies apply only when idle; copied native helper waits for original process exit, runs per-user installer, restarts application. Portable copies notify only. No forced conversion cancellation.
- [x] Optional current-user Run entry, actual registry state in settings, quoted executable; startup invocation stays in tray and respects Windows disabling startup.
- [x] Settings UI/INI, manual tray check, daily timer and cancellation on shutdown.
- [x] Policy tests, settings/UI regressions, helper handoff tests, build and release documentation.

The user supplied Rzaev125/Supertool and explicitly approved publishing all source and artifacts using existing GitHub authorization after the initial approval review rejection. All authorized publication steps are complete.

Native C++20/WinHTTP/BCrypt, no service or runtime dependency. HTTPS authenticates the configured publisher; SHA256 detects package mismatch, not a compromised publisher. Installer remains unsigned.

Completion evidence:
- All26 selected CTest checks have passing results; focused final4/4PASS. Actual user's Desktop instance preserved; application_smoke excluded.
- Real process helper success and installer-launch failure/restart tested; isolated Run key enabled/disabled without changing real startup registration.
- Independent review findings resolved, no remaining important blockers.
- Source published on main; v0.6.0 release assets uploaded with remote GitHub SHA256 matching local hashes.
- Manifest updates/stable.txt published only after release assets were public.
- Real production WinHTTP fetch and complete556011128-byte installer download passed exact size and SHA256, without running installer. Downloaded test cache removed.
- ZIP629258770bytes SHA2563B64710AFC952B6C04D30F27B49A98AF931925A93C6AD184FC9165300256A340, full CRC/EXE/docs/modules validated.
- Setup556011128bytes SHA256A6085552F75D432E6073D1681CFE89163CFD358B3D5E7E698312198C666A7ABB, embedded payload runtime matches final723456-byte EXE; full compressed archive integrity passed.
- Release: https://github.com/Rzaev125/Supertool/releases/tag/v0.6.0