# HDR precision and local release implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline.

**Goal:** Close the0.14 precision-test remark, perform available Windows acceptance and prepare current local release artifacts.

**Architecture:** Add controlled lossless RGB grayscale ramps for each source code at10/12 bits, compare raw16 decoder samples to independently known codes, prove8-bit-roundtrip mutation rejection. Existing broad color fixtures keep their codec/range tolerance. No product behavior change; retain0.14.0. Use existing core/full packaging and isolated installer harnesses.

**Tech Stack:** Python/Pillow fixtures, C++/Win32 existing build, PowerShell/Inno packaging, computer-use Windows UI.

**Spec:** User confirmed next steps after0.14; docs/review-0.14.0.md Minor precision remark, docs/superpowers/specs/2026-10-23-hdr-sdr-design.md for existing color behavior.

## Global Constraints

- Work exclusively in D:/IT/Project/Supertool, on a local branch in place; preserve older out packages and all user settings/data.
- No new production format/algorithm/version;0.14.0 has not previously been packaged here. New artifacts must be fresh and identified as unsigned/local.
- Do not create/sign with real keys or claim Authenticode, CI or clean-VM validation without credentials/environment. Do not publish or change update channels in this task.
- GUI acceptance uses only dedicated generated files and temporary app settings. Do not operate unrelated user windows or leave a dragged file moved.
- One final fresh read-only review, one consolidated confirmed Important/Critical fix pass. Minor findings recorded.

## Review Focus

- Precision test must distinguish10/12-bit actual sample values from8-bit values padded into16-bit PNG, independently of the decoder output used by the tone reference.
- Exact-fixture tolerance must account for documented native bit expansion without accepting8-bit roundtrip; lossy fixtures must not be re-labelled exact.
- UI evidence distinguishes actual Explorer drag/Shift from OLE synthetic tests; unavailable monitor/VM cases stay open.
- Packaged EXE/module bytes must match built/inventoried sources and exclude user settings; installer lifecycle must stay inside its isolated root.
- Old releases and unsigned update channels remain preserved, signatures not falsely claimed.

### Task1: Precision regression

- [x] Add dense real lossless matrix0 RGB ramps covering every10/12-bit code for PQ/HLG AVIF/HEIC and8-bit-roundtrip mutations.
- [x] Demonstrate current256code tolerance fails mutation rejection (RED), then add independent≤2code exact precision assertion (GREEN); retain broad lossy/range checks with accurate comments.
- [x] Register test in CMake, run focused HDR/precision tests and commit.

### Task2: Windows acceptance

- [ ] Run local app with test settings and open only a generated fixture folder in Explorer; check actual drag/Shift where supported by available UI API, cancellation/geometry and conversion.
- [ ] Record exact evidence and unsupported input/control or physical monitor/clean-VM constraints; restore settings and close only own test windows/app.

### Task3: Release preparation

- [ ] Prepare validation/release docs before package-policy tests; run complete CTest after UI app is closed; one read-only review.
- [ ] Build fresh core/full ZIP and installers with existing audited packaging; verify manifests/hashes and isolated install/launch/conversion/update/uninstall lifecycle where available.
- [ ] Record artifact hashes, unsigned status and remaining external signature/CI/VM steps; commit final evidence, keep branch local and archive only this plan's ledger.
