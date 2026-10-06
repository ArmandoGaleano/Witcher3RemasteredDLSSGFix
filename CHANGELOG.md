# Changelog

## v0.2.0-beta

- Extend the Frame Generation resolution fix to requested sizes whose width or height is not divisible by 4.
- Preserve the requested width/height as the maximum DRS extent instead of rounding the maximum down.
- Keep the full-resolution render-target descriptor consistent with that exact maximum extent.
- Keep lower DRS slots on the game's original aligned-size behavior; special NGX/FSR/XeSS slots are not changed by the maximum-slot correction.
- Keep the v0.1.0 legacy-DRS fix.
- Add descriptor-tail validation and runtime hook-encoding self-checks.
- Expand dry-run validation for the v0.2 patch set.
- Verified 3413x1440 with DRS max/viewport 3413x1440, zero offsets, DLSS-RR output 3413x1440 and DLSS-G `eOn`.
- Verified Frame Generation after cold start at 3620x1527 and 2560x1080.
- Verified Frame Generation remains active when switching through 3413x1440, 3620x1527, 3840x1620, 2560x1080 and 1920x1080 in the same session.
- No forced DLSS-G state, no Streamline/NVIDIA DLL replacement, and no on-disk executable modification.

## v0.1.0-beta

- Initial public beta.
- Fix legacy DRS resolution calculation preventing DLSS Frame Generation at affected resolutions.
- Runtime-only, fail-safe ASI patch selecting the game's existing non-legacy DRS path.
- Confirmed 2560x1080 fix on The Witcher 3 Remastered 5.00c, Steam build 25646871 (DX12).
