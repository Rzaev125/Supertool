# Progress — plan: docs/superpowers/plans/2026-10-22-media-office-pdf.md

- User authorized adding video, audio, DOCX, XLSX and PDF to the existing prototype.
- Native C++/Win32 resident process retained. External modules absent on PATH; obtaining portable copies inside the workspace. PDFium native DLL already available in the bundled runtime; Python is only a test-fixture tool.
- Ruling: implement PDF image/text export, not PDF-to-DOCX/XLSX reconstruction — preserves honest capabilities and avoids promising OCR/layout recovery.
- Ruling: reuse the authorized workspace; git is unborn and metadata read-only, so no worktree or commits.
- Task 1 RED: old policy rejected DOCX; GREEN: seven catalog/gesture/quoting groups pass. Dynamic wheel now uses category route vectors and rejects changed data objects at Drop.
- Task 2 RED: controlled grandchild held a nested staging file; old cancellation left staging behind. GREEN: entire job terminated and nested staging removed; original/foreign destination preserved.
- PDF real integration: both pages rendered at 144 DPI, red/blue pixel checks, all-page text; encrypted/scanned/corrupt input and existing output folder refusal pass.
- Office integration found LibreOffice leaves .~lock.*# alongside multi-sheet CSVs. Reproduced independently with native soffice.com. Fixed publication to move only CSV files into the final folder; lock remains private and is removed during cleanup.
- LibreOffice MSI SHA256 matches upstream and Authenticode signer The Document Foundation is valid. Administrative extraction succeeded; no normal global install.
- Ruling: bundled office help assets retained after extracted runtime emitted missing-XSL warnings. Runtime remains portable.
- Build attempt during a running Office test encountered the expected EXE file lock; repeated only after its conversion process exited.
- Media real integration passed: all ten video/audio target routes, decoded stream type/duration, silent video and audio-extraction failure, corrupt media and output collision.
- Office real integration passed after CSV lock fix: all nine targets, Unicode text, two-page PDFs, both spreadsheet sheets, formula values.
- Final reviewer identified PDF widget loss. RED: a filled ReportLab AcroForm was silently converted. Fix: detect form type and refuse until flattened; no claim of form rendering. Regression also covers TXT, whose form values could otherwise be missing.
- Final review second finding reproduced RED: a valid MOV container with a damaged H.264 packet was partially transcoded and reported successful. Added FFmpeg -xerror, -max_error_rate 0, -err_detect explode; all outputs remain private until successful completion.
- Final verification: Release build warning-free; complete CTest 9/9 in 58.00 seconds, including both reviewer regressions. Ten-second idle sample: 0.0 ms CPU / 10007 ms, 13.0 MiB working set. EXE 410624 bytes, system DLL imports only.
- Offscreen Direct2D 4/5-sector wheels and converted Office PDF fixture pages visually inspected. Manual Explorer gesture and mixed DPI remain explicitly pending.
- Delivery complete: out/ShiftWheel has the 410624-byte EXE and all 19302 module files (inventory/size equality and entry-point hashes verified). Portable EXE tray smoke passes: 0.0 ms CPU / 2006 ms, 12.9 MiB. ZIP CRC verification passes for 20927 entries; archive 572337611 bytes. Runtime modules occupy 1659.7 MiB on disk, not resident memory.
