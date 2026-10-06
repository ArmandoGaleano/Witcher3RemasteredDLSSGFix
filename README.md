# Witcher 3 Remastered - DLSS Frame Generation Resolution Fix

**Version:** v0.2.0-beta · **Game:** The Witcher 3: Wild Hunt — Remastered (5.00c, DX12) · **License:** MIT

A small, runtime-only ASI plugin that fixes internal resolution mismatches that can prevent NVIDIA
DLSS Frame Generation — including Multi Frame Generation — from enabling at some native, ultrawide,
DSR and DLDSR resolutions.

## Downloads

- **Nexus Mods:** https://www.nexusmods.com/witcher3/mods/13434
- **GitHub Releases:** https://github.com/ArmandoGaleano/Witcher3RemasteredDLSSGFix/releases
- **Source code:** https://github.com/ArmandoGaleano/Witcher3RemasteredDLSSGFix

> **THIS MOD DOES NOT FORCE FRAME GENERATION ON.**
> It fixes the upstream resolution state and leaves the game's own DLSS-G enable/disable logic intact.
> The game itself still decides whether Frame Generation is allowed.

## What v0.2.0-beta fixes

The game has two related resolution-alignment problems in the path used to prepare DRS/render targets
for DLSS and DLSS-G.

The original v0.1.0-beta fixed the first problem: the legacy DRS range solver could choose an internal
maximum smaller than the requested output resolution. At 2560x1080, for example, the game could use
2544x1060 internally, which caused the game's Hudless/UI validation to reject DLSS-G.

v0.2.0-beta keeps that fix and also fixes the remaining multiple-of-4 alignment case. The game's
non-legacy path normally rounds the maximum DRS extent down to a multiple of 4. That means a requested
resolution such as 3413x1440 becomes 3412x1440. The resulting 1-pixel letterbox/viewport offset is enough
for the game's DLSS-G validation to stay off.

The new version keeps the requested maximum extent exact and makes the full-resolution render target use
the same exact size, while leaving lower DRS slots at the game's original aligned sizes.

Verified example:

```text
3413x1440 requested
  -> v0.1.0-beta: DRS max 3412x1440, viewport 3412x1440, offset 1,0 -> FG off
  -> v0.2.0-beta: DRS max 3413x1440, viewport 3413x1440, offset 0,0 -> FG on
```

Ray Reconstruction was also verified at `2275x960 -> 3413x1440` with no NGX `InvalidParameter` failures.

## Why this matters for DSR / DLDSR

DSR/DLDSR can expose desktop resolutions whose width or height is not divisible by 4. Those modes could
still fail with v0.1.0-beta even though the original legacy-DRS issue was fixed.

Locally verified with v0.2.0-beta:

- **3413x1440** — cold start, Frame Generation enabled, full Streamline/NGX evidence collected.
- **3620x1527** — cold start, Frame Generation enabled.
- **2560x1080** — cold start, Frame Generation enabled.
- **3840x1620** — resolution switch, Frame Generation remained enabled.
- **1920x1080** — resolution switch, Frame Generation remained enabled.

A single `DXGI_ERROR_INVALID_CALL` was observed during one exclusive-fullscreen focus-loss transition at
3413x1440; DLSS-G re-enabled normally after focus returned and the error did not repeat. It was not observed
as a continuous failure.

## What it does NOT do

- does not replace DLSS DLLs (`nvngx_dlss*.dll`);
- does not replace or modify Streamline (`sl.*.dll`);
- does not modify NVIDIA NGX libraries;
- does not patch `witcher3.exe` on disk;
- does not modify saves or settings files;
- does not change `DLSSGUseHudless` or other DLSS-G cvars;
- does not force `slDLSSGSetOptions(eOn)`;
- does not disable the game's DLSS-G validation;
- no network access, telemetry or auto-updater.

## How it works

At startup the plugin validates the supported `witcher3.exe` build before making any runtime change.

It first selects the game's existing non-legacy DRS range path. v0.2.0-beta then applies a narrow correction
to preserve the requested width/height as the maximum DRS extent and to keep the full-resolution render
target consistent with that exact extent.

Lower DRS slots keep their original aligned dimensions. Special slots used by NGX/FSR/XeSS are not changed
by the maximum-slot correction.

All changes exist only in the running process. The executable on disk is never modified.

For the reverse-engineering details and validation criteria, see `docs/technical-details.md`.

## Safety / fail-safe behavior

The plugin validates, before patching:

- the supported x64 PE layout;
- both legacy-DRS signatures and their shared `UseLegacy` cvar;
- the cvar metadata and expected defaults;
- the complete expected non-legacy blocks before changing them;
- the render-target descriptor-tail signature and original bytes;
- the runtime hook encoding before it is installed.

The update is treated as one patch set. If validation fails, the plugin reports an unsupported build and
does not apply the fix.

The included dry-run audit tool performs the validation without modifying or running game code.

## Compatibility

Confirmed / tested:

- The Witcher 3 Remastered **5.00c**
- Steam build **25646871**
- DX12
- exclusive fullscreen (`FullScreenMode=2`) for the instrumented validation runs
- NVIDIA RTX 50-series test environment
- DLSS Ray Reconstruction + DLSS Multi Frame Generation (3 generated frames)

Tested alongside (not required, not guaranteed compatible): RenoDX / DLSS 5 setup, ReShade,
W3SpawnMenu, Ultimate ASI Loader, NVIDIA App DLSS Override / Streamline OTA plugins.

See `docs/compatibility.md` for the test matrix.

## Requirements

- The Witcher 3 Remastered DX12 executable (`bin\x64_dx12\witcher3.exe`).
- A compatible ASI Loader installed separately.
- NVIDIA GPU with DLSS Frame Generation support.

## Installation

1. Install a compatible ASI Loader for The Witcher 3 Remastered DX12 if you do not have one already.
2. Copy `Witcher3RemasteredDLSSGFix.asi` to `The Witcher 3\bin\x64_dx12\`.
3. Launch the game in DX12 and enable DLSS Frame Generation normally.
4. If something does not work, inspect `Witcher3RemasteredDLSSGFix.log` next to the plugin.

## Uninstall

Delete `Witcher3RemasteredDLSSGFix.asi` and, optionally, its log file. No save or settings cleanup is needed.

## Known limitations

- Only the supported game build above is validated.
- Game updates may require new signatures.
- Other GPUs, monitors, display modes and resolutions still need community testing.
- Instrumented validation of the new non-multiple-of-4 path was performed in exclusive fullscreen.
- This remains a beta release.

## Troubleshooting

Check `Witcher3RemasteredDLSSGFix.log`.

A successful v0.2.0-beta startup should report that all validations passed and the complete patch set was
applied. If the executable does not match the supported build, the plugin should leave the game unmodified
and report an unsupported build.

Never replace NVIDIA DLLs just to install this fix; it does not require that.

## Building from source

See `scripts/build.cmd` for the portable w64devkit/GCC build. The dry-run utility validates the supported
executable without starting the game or writing to it.

## Credits and legal

Author: Armando Galeano.

The Witcher 3 is property of CD PROJEKT RED. DLSS, Streamline and NGX are property of NVIDIA.
This project contains no files from the game, NVIDIA or any third-party mod. Source code is MIT licensed.
