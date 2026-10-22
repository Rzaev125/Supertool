# ShiftWheel Prototype Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a runnable, lightweight Windows prototype of drag + Shift -> format wheel -> image conversion.

**Architecture:** Win32 event-driven tray process, Direct2D/DirectWrite circular OLE drop target, isolated WIC worker in the same executable. Pure interaction policy separated from Windows integration.

**Tech Stack:** C++20, Win32, Direct2D, DirectWrite, WIC, MSVC, CMake, CTest, PowerShell.

**Spec:** ../specs/2026-10-22-shift-wheel-design.md

## Global Constraints

- Windows 10/11 x64; Russian UI; no admin, auto-start, polling loop or downloads.
- PNG/JPEG/BMP input and output, one local file and one worker at a time.
- Original preserved; never MOVE or overwrite; same-folder atomic publication.
- JPEG quality 0.90; white background for JPEG/BMP; at most 40 million pixels.
- Full universal conversion remains a roadmap; no UI claims for missing modules.

## Review Focus

- Shift already means MOVE in Explorer: COPY-only acceptance and cancellation.
- Mouse-up hook must not hide the target before OLE Drop arrives.
- Transparent pixels must become white, not black, in JPEG/BMP.
- Concurrent output-name collision must fail without overwriting either file.
- Background worker failure/cancellation must leave no completed-looking partial file.

## Task 1: Interaction policy and build

**Files:** CMakeLists.txt, scripts/build.ps1, src/policy.h, src/policy.cpp, tests/policy_tests.cpp.

**Interfaces:** `Format { Jpeg, Png, Bmp }`; `int sector_at(float x,float y,float inner,float outer,int count)`; `bool can_accept(bool shift,bool copy_allowed,size_t file_count,bool supported,int sector,bool busy)`; `std::wstring quote_argument(std::wstring_view)`; `std::filesystem::path output_candidate(path, Format, unsigned suffix)`.

- [x] Write failing tests: neutral center/outside -> -1; top/right/left sectors stable; COPY only, Shift only, one file, valid sector, not busy; quoting roundtrip; output names never identical to input for same format.
- [x] Run tests and observe failing assertions against minimal stubs.
- [x] Implement policy and gesture state (threshold, Shift rearm, release).
- [x] Build with /W4 and run CTest: all policy tests pass.

## Task 2: Isolated image worker

**Files:** src/image_converter.h, src/image_converter.cpp, tests/image_tests.cpp.

**Interfaces:** `void convert_image(const path& input,const path& output,const path& temporary)`; errors as exceptions, COM initialized by caller. Shared worker CLI: `ShiftWheel.exe --convert INPUT OUTPUT TEMP`.

- [x] Write failing real-file tests for white alpha JPEG, PNG alpha, corrupt file, existing output and source safety using WIC-generated fixtures in a unique test directory.
- [x] Run against a throwing stub and observe failures.
- [x] Implement bounded decode, orientation handling, encoding and atomic output publication; cleanup on exceptions.
- [x] Run all tests; inspect generated JPEG pixels and byte-identical input.

## Task 3: Tray, wheel, and OLE integration

**Files:** src/main.cpp, src/app.h, src/app.cpp, src/wheel.h, src/wheel.cpp, src/worker.h, src/worker.cpp, resources/app.manifest, resources/app.rc, tests/smoke.ps1.

**Interfaces:** `int run_app(HINSTANCE, bool preview)`; wheel consumes policy/CF_HDROP, worker consumes converter CLI; Windows messages carry event notifications, no synchronous conversion in Drop.

- [x] Define repeatable smoke acceptance checks before UI implementation: app starts and exits, one OLE dropped PNG starts exactly one COPY worker; center/Shift release cancel; no source deletion.
- [x] Implement tray, observer hooks, topmost no-activate circular Direct2D window and OLE IDropTarget.
- [x] Implement CreateProcessW quoting, Job Object, one-task admission, blocking completion wait, cancellation and notifications.
- [x] Build and run CTest plus achievable native smoke checks; report real Explorer/DPI checks separately if UI automation is unavailable.
- [ ] Manual Explorer drag and mixed-DPI acceptance (explicitly pending; automated tests establish COM contract and separate worker, not the whole desktop gesture).

## Task 4: Review and delivery

**Files:** README.md, docs/verification.md, docs/progress.md.

- [x] Fresh review of source, spec and tests. Fix material findings with regression tests.
- [x] Run final Release build, CTest, worker smoke and idle resource measurement.
- [x] Document exact build/run commands, limitations, measured results and manual checks still needed.
- [x] Provide executable path and truthful prototype status. No commits/branch mutations in the read-only .git.
