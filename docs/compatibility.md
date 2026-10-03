# Compatibility

## Supported game build (v0.1.0-beta)

| Item | Value |
|---|---|
| Game | The Witcher 3: Wild Hunt — Remastered |
| Version | 5.00c (in-game "v 5.00c") |
| Steam build | 25646871 |
| Executable | `bin\x64_dx12\witcher3.exe`, FileVersion 5.0.0.1044392 / 5.0.15.61352 |
| Executable SHA256 | `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51` |
| Renderer | DX12 only (the DX11 executable is not supported and not patched) |

Other builds: the plugin validates signatures and cvar metadata and applies **nothing** when they do not
match (`unsupported game build` in the log). Future game updates may need updated signatures.

## Resolutions

| Resolution | Vanilla internal extent | Expected with fix | Status |
|---|---|---|---|
| 2560x1080 | 2544x1060 (FG off) | 2560x1080 | **Confirmed** (FG on, 3 generated frames) |
| 1920x1080 | 1920x1080 (FG on) | 1920x1080 | Unaffected |
| 1680x1050 | 1680x1008 | 1680x1048 (non-legacy rounds to multiples of 4) | Not tested with fix; FG may stay off |
| 1600x1024 | 1600x1000 | 1600x1024 | Not tested with fix |
| 1600x900 | 1584x880 | 1600x900 | Not tested with fix |
| 2560x1440, 3440x1440, 3840x1600, 3840x2160 | predicted exact even in vanilla | unchanged | Not tested |
| 1280x720, 1366x768 | predicted 1260x720 / 1344x768 | 1280x720 / 1364x768 | Not tested |

Community reports for other resolutions are welcome (include the plugin log and, if possible, the
Streamline log).

## Display modes

Tested in exclusive fullscreen (`FullScreenMode=2`). Borderless and windowed modes were not tested with
the fix.

## Tested alongside (not required, not guaranteed)

The test environment had the following installed at the same time; the fix does not depend on them and
does not interact with them:

- RenoDX / "DLSS 5" ReShade add-on (hooks NGX create/evaluate in a different module);
- ReShade 6.8 (`dxgi.dll` in `bin\x64_dx12`);
- W3SpawnMenu (`W3SpawnMenu.asi`, loaded by the same ASI loader);
- Ultimate ASI Loader (`dinput8.dll`);
- NVIDIA App DLSS Override with Streamline OTA plugins (`sl.*` 2.14.0 loaded from `ProgramData` instead
  of the game's 2.14.1) and `nvngx_dlssg.dll` 310.9.1.

Anything that modifies the same two code sites of `witcher3.exe` would conflict; no known mod does.

## Settings interaction

- `[Rendering/DRS] Enable=false` is the state the in-game UI enforces when DLSS is active and the state
  used in testing. A manually forced `Enable=true` runs the legacy DRS controller on top of the non-legacy
  range and is not supported in this release.
- `DLSSGUseHudless`, `DLSSGMode`, `DLSSGNumFramesToGenerate`, Reflex and all other settings are not
  touched by the plugin.

## Hardware

Tested on an NVIDIA RTX 50-series GPU with DLSS Multi Frame Generation (3 generated frames). RTX 40-series
single-frame generation should behave identically from the game's point of view but was not tested.
