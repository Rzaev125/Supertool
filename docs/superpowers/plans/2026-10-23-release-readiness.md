# Release readiness follow-up

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline.

**Goal:** Close the reproduced stale release-default gap and identify available environmental acceptance without changing the verified0.14 binaries.

**Architecture:** Standard-library Python guard compares current version declarations/defaults and runs before signing/bootstrap in the existing workflow. Existing signing gates remain unchanged. Record available Windows/VM/signing configuration and the user's physical Shift-drag observation separately from automated tests.

**Spec:** User requested continuation of remaining Windows acceptance and signed-release preparation; docs/artifacts-0.14.0.md lists those gates. Small corrective change, no new product design.

## Constraints / review focus

- Exact D:/IT/Project/Supertool checkout on a local branch, no publication, secrets creation/import or system configuration changes.
- Runtime0.14 code, existing ZIP/setup/source artifacts and update channels remain unchanged.
- Guard covers actual current version declarations/defaults, not historical docs; no third-party Python dependency or private credential reads.
- Missing real keys/VM/physical input are explicit open gates, never assumed passed. User-requested manual test may be in progress: do not stop that app or run conflicting tray tests.
- Verify RED on real stale defaults, GREEN on consistent current versions, focused signing policy tests, one fresh read-only review. No whole runtime rebuild/regression for CI-only changes without a new failure.

### Task1: Release default guard

- [x] Add tests/release_config_tests.py covering current CMake/EXE/resource/installer/package/docs/default workflow versions; expect0.11/0.8 mismatches (RED).
- [x] Align only stale defaults to0.14 and run guard before existing release configuration/signing gates. GREEN and missing/mismatched declaration mutation checks.

### Task2: Available external readiness

- [ ] Read-only OS/monitor/VM availability and signing-variable presence checks; no private content or VM starts.
- [ ] Collect user's actual test-file Shift-drag result if supplied. Keep missing real keys and clean-VM/mixed-DPI/disk-full cases open.

### Task3: Verify and record

- [ ] Run CI guard and focused existing signature/update tests, YAML syntax check if parser available; one fresh read-only review.
- [ ] Document exact results/limits, commit locally, archive this plan's ledger. Preserve all frozen0.14 release artifacts.
