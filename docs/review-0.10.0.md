# Review 0.10 preview

Fresh-context read-only review: 0374782..460304a, one pass. Critical: none. Important: none. No runtime fixes requested. Final pre-review CTest: 37/37 PASS, 159.83 s; /W4 build clean. Code unchanged after this run.

Final: minor (deferred): README conversion matrix omits GIF and its old WebP restriction paragraph contradicts 0.10. The release notes and docs/animated-images.md are authoritative for this preview; GIF and animated WebP are supported with the documented bounds.
Final: minor (deferred): add a dedicated blended WebP subrectangle fixture comparing every composited frame; current frame/alpha/disposal fixtures pass, but this exact blend case has not been independently asserted.

Final: Ruling: real-key signing and release publication — preview will remain unsigned because no real keys were supplied; publication is checked separately with remote hashes and anonymous availability — cost if wrong: Windows trust warnings or a broken download; no stable rollout.
Final: Ruling: physical drag/Shift, mixed DPI and clean Windows VM — remain explicitly unverified manual acceptance; native preview rendering and automated lifecycle do not establish these behaviors — cost if wrong: gesture, layout or environment failures on affected Windows systems.
Final: Ruling: HDR, broader EXIF/RAW and extra video codecs — outside this first animation increment, existing refusals and format capabilities stand — cost if wrong: unsupported files still require another converter.
Final: Ruling: final tests and packages not executed by reviewer — author verifies the actual full suite and exact payload/hash checks; reviewer approval alone is not evidence of their success — cost if wrong: a packaging or environment regression can escape this preview.

No second review or minor-polish fix pass. CAB tests reproduce a real sharing violation before the fix; install success, retained-cache warning, retirement and later ownership-verified cleanup pass afterward.
