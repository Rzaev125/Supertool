# Updates and Windows startup

User authorized continuation of the roadmap. Implement inline using executing-plans.

- [x] Strict HTTPS release manifest (version/url/size/SHA256), bounded streaming download and hash verification; background checks, no UI blocking.
- [x] Installed copies apply only when idle; copied native helper waits for original process exit, runs per-user installer, restarts application. Portable copies notify only. No forced conversion cancellation.
- [x] Optional current-user Run entry, actual registry state in settings, quoted executable; startup invocation stays in tray and respects Windows disabling startup.
- [x] Settings UI/INI, manual tray check, daily timer and cancellation on shutdown.
- [x] Policy tests, settings/UI regressions, helper handoff tests, build and release documentation.

The user has been asked for the HTTPS channel location; until supplied the channel remains empty and no automatic network request occurs. Native C++20/WinHTTP/BCrypt, no service or runtime dependency. HTTPS authenticates the configured publisher; SHA256 detects transport/package mismatch, not a compromised publisher. Default installer remains unsigned. Do not invent a production endpoint or claim live delivery tested without one.

Publication remains pending explicit approval of the public payload after automatic approval review rejected publishing all source files and inspecting local GitHub credentials. No source or artifact was uploaded. Requested repository is Rzaev125/Supertool, public and accessible via connector. Local manifest generation/strict parser roundtrip passed; production HTTPS channel cannot be called live until publication is confirmed.
