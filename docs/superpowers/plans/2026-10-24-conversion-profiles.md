# Conversion profiles implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan inline, task by task, then one fresh whole-change reviewer. The owner explicitly approved the complete design and requested implementation; no additional execution-method approval is needed.

**Goal:** Add category profiles and truthful per-task execution details without expanding the wheel.
**Architecture:** Settings owns validated profiles; a pure resolver produces per-job option snapshots at admission. Native settings edits a draft. Bounded structured worker events report completed engines; queue history owns measured facts.
**Tech Stack:** Existing C++20, Win32, CMake, native tests and maintained local Python/PowerShell checks.
**Spec:** [Approved design](../specs/2026-10-24-conversion-profiles-design.md).

## Global constraints

- Work only in the current D:/IT/Project/Supertool checkout/feature branch; local commits, no publication or stage 6.
- Keep version 0.16.0, current modules, private Office profiles, sequential queue, quiet notifications, folder grouping and settings.
- All manual tests are skipped by the owner; automated HWND/layout and actual fixture conversions remain in scope.
- No new runtime dependencies, polling, user-file probing for profile hints or automatic module installation.
- Old INI and SWQ1/2/3 states remain readable; 32 profiles, 64 UTF-16-unit names, 64-KiB INI and existing queue bounds.

## Review Focus

- Active profile target hidden or module unavailable must never resurrect a sector or silently change format (task 1 resolver/policy tests).
- Editing/deleting a profile after admission and retry/recovery must preserve effective options and original profile label (task 2 queue tests).
- Hidden-section drafts, invalid editor values during switching, and a progress update must not discard edits (task 3 panel contracts).
- GPU startup/probe failure must report the successful CPU backend rather than merely detected GPU (task 4 protocol/media integration).
- Cancellation, multi-file/page output, malformed telemetry and old history must not invent measured facts or traverse foreign paths (tasks 2/4).

## File responsibilities

- `src/profiles.{h,cpp}`: validation, category field resolution, templates and contextual consequences; uses `options.h` model.
- `src/options.{h,cpp}`: profile model, optional INI keys and atomic persistence.
- `src/job_queue.{h,cpp}`, `src/job_store.cpp`, `src/app.cpp`: all-or-nothing per-input admission, immutable labels, backward-readable persisted facts, details.
- `src/control_panel.cpp`: native profile draft editor in Conversion; retain common parameter controls.
- `src/execution.{h,cpp}`, `src/worker.{h,cpp}`, converters: bounded engine events and measured completion details.
- Existing tests extended where they own the behavior; `tests/profiles_tests.cpp` and profile execution integration are new. CMake registers them.

### Task 1: Model, INI and resolver

Interfaces: `ConversionProfile { id, name, category, format, options }`; Settings gains profiles and active_profiles. `resolve_profile(input, format, settings)` returns options/profile ID/name. `profile_formats(input, settings)` prioritizes only a currently configured sector. `validate_profiles`, `profile_consequences`, `make_profile` own policy.

- [ ] Add failing native profile tests for Unicode round-trip, old INI defaults, 32/33 and names/IDs/duplicate/link limits, explicit format compatibility, inactive target, unrelated category fields and snapshot semantics.
- [ ] Build/run to confirm missing API or contract failure; save RED log.
- [ ] Implement profile helpers and strict optional INI serialization (all profile option fields explicit), reuse atomic save and old global option parser. Do not accept profile fields in worker option-only snapshots.
- [ ] Run profile/options/policy tests; confirm GREEN and settings fixture integrity. Commit model/storage/resolver.

### Task 2: Queue snapshots and saved facts

Interfaces: `enqueue_grouped(..., const Settings&)` resolves per input and assigns immutable labels; existing options overload remains. ConversionJob gains profile ID/name and optional `ExecutionFacts`. WorkerResult carries facts. Queue format SWQ4 reads SWQ1/2/3. Details format missing values explicitly.

- [ ] Add failing queue/store tests: category resolution, group atomicity, rename/delete after admission, retry/copy labels, history round-trip and old-state absence, malformed stats and paths.
- [ ] Run tests and observe RED.
- [ ] Implement queue admission and SWQ4 bounded optional data. Reset execution facts on retry/start; preserve labels. Existing enqueue overload stays compatible with developer probes.
- [ ] Run queue/job_store/batch_output/profiles GREEN; commit.

### Task 3: Native profile draft editor

Interfaces: existing PanelActions.save receives complete Settings. Existing common controls retain IDs/contracts. New controls use IDs >=400; editor owns independent selection/draft and category-specific parameter visibility.

- [ ] Extend panel tests to create/template/duplicate/delete/save, select active, reject invalid transitions without losing the editor, retain common settings on progress, hide irrelevant controls and verify scroll/DPI layout.
- [ ] Run RED before creating controls.
- [ ] Add editor and consequences block at top of Conversion, plus common settings below. Save editor before selection changes/Save. Explicit active choice, format availability explanation, defaults retains definitions and clears active map.
- [ ] Run panel_contract plus profiles/options GREEN, automatic native layout checks at 96/144/192 DPI. Commit.

### Task 4: Actual execution details and final verification

Interfaces: bounded execution event type distinct from SWP1; engine identifiers from a known allowlist and at most16 stages. Worker measures elapsed and input/output bytes once, preserves already completed stages on error/cancel. Successful encoder branch records actual backend and fallback.

- [ ] Add RED tests for valid/malformed execution messages, no fabricated backend, CPU-after-GPU fallback and cancelled/failed sizes; add real profile conversion helper/integration using production Worker and queue resolver.
- [ ] Implement events and record completed branches in native/bridge/Office/PDF/OCR/media routes. Extend worker reader without weakening progress limits. Keep children from inheriting report stdout. Measure only owned published output; skip links.
- [ ] Wire app admission and retry to profile snapshots and facts; show name/engine/backend/elapsed/bytes in existing task detail only.
- [ ] Run focused protocol, media, profile execution and panel tests GREEN; build final app. Gracefully close only the exact local idle app if necessary, preserve INI, then start normal final EXE.
- [ ] Run maintained Full checks, fresh read-only review of BASE..HEAD plus uncommitted final changes. Fix material review findings with RED/GREEN, retain explicit skips. No manual or public operations.
- [ ] Mark approved spec/roadmap complete only after checks; save user documentation, evidence and local commits. Leave ordinary app running and working tree clean.

## Plan review and execution record

Self-review: all seven design sections map to the four tasks. Per-input snapshots require queue overloads rather than reusing a single options snapshot for a mixed group. Optional facts must remain absent for legacy history. Implementation runs inline under existing local-work authorization; design was explicitly approved by owner.
