# Review 0.11 preview

One fresh-context read-only review, ee527ee..867427f. Critical: none. Important: none. Implementation ready for preview; no runtime fix pass required. Final full CTest: 38/38, 186.24 s; /W4 build clean. No runtime changes after that run.

Prior review items resolved: README matrix/restrictions now include GIF and animated WebP; explicit offset/overlap ANMF alpha-over fixture compares every composited pixel exactly on round-trip and detects a no-blend mutant. Independent math allows up to two rounding levels after two overlaps.

Final: minor (deferred): acceptance-0.11.0.md:5 overstates PSNR coverage. Exact evidence: all CPU/container/preset combinations receive structural/full-decode checks; PSNR >25 dB is measured only on balanced Auto MP4 per codec (HEVC NVENC, AV1 CPU locally). This paragraph corrects the interpretation for preview readers; extending PSNR to every CPU preset is future coverage.

Final: Ruling: successful AV1 GPU and AMD AMF — remain explicitly unverified; failed local initialization safely uses the same codec on CPU — cost if wrong: another GPU/driver may reject options or fall back more often, reducing speed.
Final: Ruling: HDR/tone mapping, 10/12-bit preservation and metadata — remain excluded from this SDR increment; use SDR sources — cost if wrong: HDR color or metadata may be lost, so these sources need another workflow.
Final: Ruling: physical Explorer drag/Shift and mixed DPI — remain manual acceptance; automated/native-contract checks do not prove physical interaction — cost if wrong: gesture or layout problems on affected desktops.
Final: Ruling: clean VM and real inter-version deployment — local isolated install/upgrade/uninstall passes but does not prove a clean Windows environment — cost if wrong: machine-specific dependencies or startup/update failures.
Final: Ruling: production signing and keys — no real keys supplied; installers stay unsigned and no stable rollout occurs — cost if wrong: Windows trust warnings; signatures are not claimed from hashes alone.
Final: Ruling: final inventories/remote hashes/public availability — author must verify exact ZIP/setup/source bytes and public links before publication is considered complete — cost if wrong: a broken package or download can escape the preview.
Final: Ruling: arbitrary long/high-resolution quality/speed/memory — short synthetic SDR tests are bounded samples, not a general benchmark; CPU thread limits are not memory/power guarantees — cost if wrong: long conversions can be slow or consume more RAM than expected.
Final: Ruling: pending review placeholder — replaced by this committed review before final staging; immutable artifacts are checked afterward — cost if wrong: incomplete review evidence could be shipped.

No second review. Packaging and publication acceptance are recorded separately in release-0.11.0.md on the source branch after publication; historical review-0.10.0.md is preserved.
