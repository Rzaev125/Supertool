# Dark settings implementation plan

> For agentic workers: use superpowers:executing-plans for inline execution.

**Goal:** Refresh Supertool settings with the dark interface requested by the owner.
**Architecture:** Keep the native Win32 controls and current settings/action contracts. Use a persistent owner-drawn list box for five sections, themed native form controls, a fixed save footer and a scrollable content page. No web runtime or background animation.
**Tech stack:** C++20, Win32/GDI, ComCtl32, documented DWM title-bar API.
**Spec:** Owner chose a dark interface; scope is the existing settings screen and the shared window chrome.

## Design and constraints

- Canvas #11151C, surface #1B222D, border #334155, text #EDF3FA, muted #9BAAC0, accent #5CA4FF. Segoe UI 11pt; larger page headings. The blue/turquoise logo is the sole decorative brand element.
- Header: logo/name at left, Tasks/Settings at right. Left rail: General, Conversion, Wheel, Updates, Modules. Right: section title, brief hint, meaningful groups of controls. Fixed bottom: feedback, defaults, save.
- Keep save-on-explicit-action, draft preservation, all formats and metadata scope labels, existing callbacks and validation. Preserve tab focus, native accessibility roles, DPI scrolling and high-contrast system colors.
- Native owner draw only where necessary; no private UxTheme ordinals. No global mouse or keyboard hooks added. No timer-driven repaint.
- Prepare version 0.16.0; preserve frozen 0.15.x packages and update channels. New Full packaging/publication is outside this design task.

## Tasks

- [x] Add native navigation/layout regression checks and an offscreen render mode to the existing panel harness. Observe the old UI fail the new navigation requirement.
- [x] Apply dark native control drawing, section navigation, grouped layout and fixed footer; render and inspect actual controls, including all five sections and 144/192 DPI.
- [x] Build the application, run the native panel contract and relevant integration checks, check version declarations. Fix any regressions and inspect the final screenshots.
- [x] Obtain one final read-only review, address material findings, record results and commit the source changes. Deliver the new EXE and settings preview.

## Review focus

Hidden-section drafts and callbacks; long metadata/Unicode text; keyboard focus after scrolling; minimum-size and 150/200% DPI layout; dark combo dropdowns and disabled/selected text; Windows high-contrast fallback.

## Execution record (completed 2026-10-24)

- Baseline 6bc47f6. Initial RED: build/settings16-red.log. Navigation tests were adapted from combo messages to real LISTBOX selection messages; settings control IDs and save/action contracts remain unchanged.
- Added a separate panel_theme unit. Dark native form drawing is driven by repaint/focus events, not a background timer. System high-contrast colors override the palette and restore native page scrolling; live global high-contrast switching was not performed.
- Actual native controls were rendered into a bitmap from a hidden test host, then converted to PNG. No screenshot of another application or of the user's desktop was taken. Views cover all five sections, the bottom of Conversion, 96/144/192 DPI and the narrow 740-DIP layout. These are simulated DPI scenarios, not a claim of mixed physical monitor acceptance.
- Full CTest before review fixes: **46/46 PASS, 256.66s**, build/settings16-full-tests.log. This includes real image/media/office/PDF/OCR conversions.
- One independent read-only review found two P2 layout defects: Tasks at minimum height and a stale high-contrast scroll range after scrollbar width changes. The first was reproduced with HWND rectangles (build/settings16-review-red.log), then fixed by adapting list/detail height. Scroll range now measures again after native scrollbar visibility settles. A narrow-layout check verifies that the last OCR checkbox fits at the scroll bottom; the high-contrast width branch was checked in source, without modifying global Windows settings.
- Final application build PASS. After the fixes: **21 native panel cases PASS**, build/settings16-panel-final.log; **9/9 related CTest checks PASS, 16.43s**, build/settings16-final-focused.log. Release version guard and actionlint PASS; git diff --check clean.
- Final EXE: build/Release/Supertool.exe, ProductVersion0.16.0. Main preview: build/settings16-general.png; narrow DPI preview: build/settings16-wheel-144.png. The offscreen helper is tests/panel_tests.cpp --render-settings OUTPUT.bmp SECTION DPI WIDTH [bottom].
- Ready-made 0.15.x packages and updates/stable*.txt were not changed. No release was published; no new installer or Authenticode signing was performed in this design task.
