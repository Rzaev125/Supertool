# Supertool 1.0.0 replacement release

> Execute inline with superpowers:executing-plans; one final read-only review. The owner explicitly requested deleting the old release and publishing the new one as 1.0.0, superseding the initial 1.0.1 assumption. Build and verify everything before removing the public release/tag.

**Goal:** GitHub downloads install and launch Supertool, with Supertool-named Core/Full installers and ZIPs, at version 1.0.0.
**Architecture:** Separate production build with existing public pin and online modules; retain working local app/settings. Reuse immutable module contents/hashes, publish new module names with old CAB aliases for installed clients, sign the replacement manifest with the existing key. Replace only v1.0.0 release/tag, never force main; verify public assets before updating channels.
**Spec:** Owner's current request; [branding compatibility](../../branding.md).
**Stack:** Existing C++/Win32/CMake, PowerShell package/installer/signing scripts, Python publisher and standard existing GitHub authorization.

## Constraints

- User waived manual tests; no clean Windows VM, Windows10 or physical DPI claims.
- No Authenticode certificate; EXE/installers remain explicitly unsigned. Strict release CI gates stay intact.
- Same version means already installed 1.0.0 will not offer this as a newer update. Owner informed; manual install required.
- Existing signing key and public pin only; never print/store/publish private material. Existing CAB bytes/SHA/receipts unchanged.
- Keep old CAB names as compatible module aliases, not old application packages. New catalog names start Supertool. Archive old release metadata locally before deletion.
- Preserve user's INI and existing local app. Keep transient compiler/test trees bounded using maintained scripts.

## Tasks

- [x] Align current release notes/README/START/package docs; update catalog URLs to Supertool module names at v1.0.0, stage byte-identical CAB aliases; run version/tooling guards.
- [x] Production OFF build, Full61/61 PASS460.83s, Core/Full installers+ZIP audited45/19545files, both isolated lifecycles and installed Full media PASS, existing-key native signature PASS. [Evidence](../../verification-supertool-release-1.0.0.md).
- [ ] Fresh read-only whole-change review; commit source, generate exact source archive/checksums and publisher preflight. Backup old public release metadata. Only then delete old release/tag, push main and new v1.0.0 tag without forcing main, upload and publish new release.
- [ ] Verify every public asset digest/size/link and both public channels/native signature; sync channel commits, record validation, restore local app, clean only owned test workspaces.
