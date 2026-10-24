# ShiftWheel 1.0.0 first release

> Execute inline with superpowers:executing-plans; one final read-only review before publication. The owner explicitly requested publishing the first version to GitHub, authorizing source/tag/assets publication. Manual tests remain waived.

**Goal:** Publish ShiftWheel 1.0.0 — «Первая версия» in Rzaev125/Supertool.
**Architecture:** Keep the running developer build and settings; build a separate production candidate with online modules/updates and the existing public verification pin. Package Core/Full ZIPs and installers, sign the update manifest using the existing key, verify assets before publishing channels.
**Stack:** Current C++/Win32/CMake, maintained Full checks, pinned Inno Setup, existing ECDSA signer and GitHub publisher.

## Constraints and decisions

- No new signing keys or silent key rotation. Existing production pin must match the documented fingerprint. Never publish private keys, tokens, INI, queue or diagnostics.
- No Authenticode certificate is configured: EXE/installers remain unsigned and the release notes state this explicitly. Preserve the stricter certificate-required CI workflow; this manual release does not remove its gates.
- Module CAB contents/hashes/version0.9.0 stay immutable; their URLs move to assets on v1.0.0 so Core installation works without removed historical releases.
- Existing local build remains local-development ON. Candidate uses OFF and the real public verification pin in a distinct build directory.
- Publish packages first, verify digests/public availability, then signed/legacy update channels. No force push or overwriting existing releases.

## Tasks

- [x] Verify repository/main ancestry, empty release list, existing GitHub authorization and only the two documented public key variables.
- [x] Align version declarations/defaults, document first release/limitations, add separate build input to packaging, preserve immutable CAB pins while correcting publication URLs and catalog generation; test changed packaging/publisher contracts.
- [x] Build candidate, reuse verified module staging, run Full automatic checks and available isolated installer checks. Build Core/Full ZIPs/installers; verify package inventory, state/secret exclusions and existing-key manifest signature. Full 60/60 PASS, 319.37s, no automatic skips. [Evidence](../../verification-1.0.0.md). Manual/VM acceptance is explicitly not claimed. Source archive is generated from the committed revision in the publication step.
- [x] Fresh read-only review completed with no actionable findings. Source committed as `ebafb9530af72193567ffd815d0c850627b76ed4`; source ZIP contains exactly 329 tracked files. Atomic non-force push of main and immutable v1.0.0 tag succeeded. Published as «ShiftWheel 1.0.0 — Первая версия» using existing authorization.
- [x] All 26 GitHub assets match local SHA-256/size; all public HEAD checks succeeded before signed and legacy channels were updated. Independent verification confirmed latest/non-draft/non-prerelease state, exact public channel bytes, native manifest signature and tag commit. Owned failed test/isolated installer trees removed; original local app restored with unchanged INI. [Published release](https://github.com/Rzaev125/Supertool/releases/tag/v1.0.0).
