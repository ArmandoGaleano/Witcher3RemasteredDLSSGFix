# Changelog

## v0.1.0-beta

- Initial public beta.
- Fix legacy DRS resolution calculation preventing DLSS Frame Generation at affected resolutions.
- Runtime-only, fail-safe ASI patch (two `JE` → `JMP` byte changes in process memory, all-or-nothing,
  no patch on unsupported builds).
- Confirmed 2560x1080 fix on The Witcher 3 Remastered 5.00c, Steam build 25646871 (DX12).
