# Compatibility

## Supported game build (v0.2.0-beta)

| Item | Value |
|---|---|
| Game | The Witcher 3: Wild Hunt — Remastered |
| Version | 5.00c |
| Steam build | 25646871 |
| Executable | `bin\x64_dx12\witcher3.exe`, FileVersion 5.0.0.1044392 / 5.0.15.61352 |
| Executable SHA256 | `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51` |
| Renderer | DX12 only |

Other builds receive no patch if the signatures/semantic checks do not match.

## Resolution test matrix

| Resolution | Test type | Result with v0.2.0-beta candidate |
|---|---|---|
| 3413x1440 | cold start + instrumented Streamline/NGX run | **Confirmed: FG on** |
| 3620x1527 | cold start | **Confirmed: FG on** |
| 2560x1080 | cold start | **Confirmed: FG on** |
| 3840x1620 | in-session resolution switch | **Confirmed: FG remained on** |
| 1920x1080 | in-session resolution switch | **Confirmed: FG remained on** |

The same session also switched through all five resolutions above without visible rendering corruption
or a crash.

### Instrumented 3413x1440 result

| Item | v0.1.0-beta | v0.2.0-beta candidate |
|---|---:|---:|
| Requested / swapchain | 3413x1440 | 3413x1440 |
| DRS maximum | 3412x1440 | **3413x1440** |
| Viewport | 3412x1440 | **3413x1440** |
| Letterbox offsets | 1,0 | **0,0** |
| Full-resolution RTs | 3412x1440 | **3413x1440** |
| DLSS-RR output | 3412x1440 | **3413x1440** |
| Failed RR evaluations | 0 | **0** |
| NGX `InvalidParameter` | 0 | **0** |
| DLSS-G feature | not created | **created** |
| Interpolation state | never enabled | **eOn / 3 generated frames** |
| Hudless / Alpha UI | mismatch path | **present (`HasHudless=1`, `HasAlphaUI=1`)** |

One isolated `DXGI_ERROR_INVALID_CALL` occurred during an exclusive-fullscreen focus-loss transition.
DLSS-G had just disabled because the window was not focused, then re-enabled normally when focus returned.
The error did not repeat and is being tracked as a non-blocking observation.

## DSR / DLDSR

v0.2.0-beta specifically addresses the remaining case where the requested resolution is not divisible by
4 and the game's non-legacy path would otherwise round the maximum DRS extent down.

3413x1440 and 3620x1527 were verified locally. Other DSR/DLDSR combinations are welcome for community
testing.

## Display modes

The instrumented validation runs used exclusive fullscreen (`FullScreenMode=2`). Borderless/windowed
behavior of the new exact-maximum path has not been validated to the same level.

## Tested alongside (not required, not guaranteed)

- RenoDX / DLSS 5 ReShade add-on;
- ReShade 6.8;
- W3SpawnMenu;
- Ultimate ASI Loader;
- NVIDIA App DLSS Override / Streamline OTA plugins.

## Settings interaction

The plugin does not alter `DLSSGUseHudless`, `DLSSGMode`, `DLSSGNumFramesToGenerate`, Reflex or user
graphics settings. It does not force Frame Generation on.

## Hardware

Validated on an NVIDIA RTX 50-series GPU with DLSS Multi Frame Generation (3 generated frames).
RTX 40-series behavior is expected to follow the same game path but was not directly tested.
