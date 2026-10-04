# Witcher 3 Remastered - DLSS Frame Generation Resolution Fix

**Version:** v0.1.0-beta · **Game:** The Witcher 3: Wild Hunt — Remastered (5.00c, DX12) · **License:** MIT

A small, runtime-only ASI plugin that fixes a resolution-range calculation inside the game which can
prevent NVIDIA DLSS Frame Generation (including Multi Frame Generation) from ever being enabled at
some resolutions, for example 2560x1080.

## Downloads

- **Nexus Mods:** https://www.nexusmods.com/witcher3/mods/13434
- **GitHub Releases:** https://github.com/ArmandoGaleano/Witcher3RemasteredDLSSGFix/releases
- **Source code:** https://github.com/ArmandoGaleano/Witcher3RemasteredDLSSGFix

> **THIS MOD DOES NOT FORCE FRAME GENERATION ON.**
> It fixes the upstream resolution calculation and leaves the game's original DLSS-G safety/validation
> logic completely intact. The game itself decides to enable Frame Generation, exactly as it does at 1920x1080.

## What it fixes

The Witcher 3 Remastered contains a *legacy* dynamic-resolution (DRS) range calculation that is active by
default (`Rendering/DRS/Legacy UseLegacy = 1`, a cvar the settings loader does not accept from
`dx12user.settings`). That legacy solver rounds the maximum internal rendering extent to a multiple of a
"step" it chooses, which can make the internal extent differ from the actual swapchain resolution.

Confirmed during the investigation:

```
2560x1080 swapchain
  → legacy DRS maximum        2544x1060
  → Hudless / UI resources    2544x1060
  → DLSS-G validation rejects the mismatch (hudless must equal the viewport)
  → Frame Generation stays disabled (the game never sends mode = On)
```

With the fix, the two range calculations take the game's existing **non-legacy** path:

```
2560x1080 swapchain
  → internal extent           2560x1080
  → Hudless / UI resources    2560x1080
  → the original Hudless validation succeeds
  → the game itself enables DLSS Frame Generation normally (3 generated frames confirmed)
```

Other resolutions are affected by the same solver (observed in testing: 1680x1050 → 1680x1008,
1600x1024 → 1600x1000, 1600x900 → 1584x880). 1920x1080 happens to come out exact, which is why Frame
Generation worked there and nowhere else.

## What it does NOT do

- does not replace DLSS DLLs (`nvngx_dlss*.dll`);
- does not replace or modify Streamline (`sl.*.dll`);
- does not modify nvngx libraries;
- does not patch `witcher3.exe` on disk;
- does not modify saves or settings files;
- does not change `DLSSGUseHudless` or any other cvar;
- does not call `slDLSSGSetOptions(eOn)` or force Frame Generation;
- does not disable the game's DLSS-G validation;
- no network access, no telemetry, no auto-updater.

## How it works

At runtime, on a worker thread (outside the DLL loader lock), the plugin performs strict signature and
semantic validation against the two legacy-DRS branch sites inside `witcher3.exe`. Only if **all**
validations pass does it temporarily change

```
JE  +0x21   (74 21)
→ JMP +0x21   (EB 21)
```

at both sites, in process memory only. This is logically equivalent to selecting the game's existing
non-legacy DRS range path (the same code the game runs when `UseLegacy` is false). The executable on
disk remains untouched; the change exists only while the game is running.

## Safety (fail-safe design)

- each of the two signatures must occur **exactly once** in the game's code section;
- both branch sites must reference the **same** `UseLegacy` global;
- the cvar metadata of that global must match: group `Rendering/DRS/Legacy`, name `UseLegacy`,
  flags `0x200`, default `1`;
- the expected original opcodes (`74 21`) must be present at both sites;
- the patch is all-or-nothing: if the second write fails the first is reverted;
- if any validation fails, **no patch is applied**, the game runs unmodified and the log reports
  `unsupported game build`.

## Compatibility

Confirmed / tested:

- The Witcher 3 Remastered **5.00c**, Steam build **25646871**, DX12
- 2560x1080, fullscreen, DLSS (DLAA) + Ray Reconstruction, DLSS Multi Frame Generation (3 generated frames)
- NVIDIA RTX 50-series test environment

Tested alongside (not required, not guaranteed compatible): RenoDX / DLSS 5 setup, ReShade,
W3SpawnMenu, NVIDIA App DLSS Override / Streamline OTA plugins.

See `docs/compatibility.md` for details and the current list of resolutions with known results.

## Requirements

- The Witcher 3 Remastered, DX12 executable (`bin\x64_dx12\witcher3.exe`).
- A compatible **ASI Loader** installed separately (for example a `dinput8.dll`-based Ultimate ASI Loader
  placed in `bin\x64_dx12`). The loader is a third-party component and is **not** included in this package.
- NVIDIA GPU with DLSS Frame Generation support (RTX 40/50 series).

## Installation

1. Install a compatible ASI Loader for The Witcher 3 Remastered (DX12) if you do not have one already.
2. Copy `Witcher3RemasteredDLSSGFix.asi` to `The Witcher 3\bin\x64_dx12\` (next to `witcher3.exe`).
3. Launch the game using DX12 and enable DLSS Frame Generation in the graphics options as usual.
4. If something does not work, check `Witcher3RemasteredDLSSGFix.log` in the same folder.

## Uninstall

Delete `Witcher3RemasteredDLSSGFix.asi` (and, optionally, `Witcher3RemasteredDLSSGFix.log`).
No save or settings file needs to be changed.

## Known limitations

- tested on the supported build listed above; other builds receive **no patch** by design;
- game updates may change the code and require a new signature (the log will say `unsupported game build`);
- the initial public version is a **beta**: other resolutions, GPUs and monitors need community testing;
- resolutions whose width or height is not a multiple of 4 are rounded down to a multiple of 4 by the
  game's non-legacy path (for example 1680x1050 → 1680x1048), so Frame Generation may still stay off there;
- the DRS controller behaviour with a manually forced `[Rendering/DRS] Enable=true` is not a supported
  configuration for the initial release (the in-game UI disables DRS when DLSS is active).

## Troubleshooting

Open `Witcher3RemasteredDLSSGFix.log` next to the plugin:

- `patch applied: A 74 21 -> EB 21 ; B 74 21 -> EB 21` — the fix is active;
- `unsupported game build` — your executable does not match the tested build; nothing was changed;
- no log file at all — the ASI Loader did not load the plugin (check the loader installation and that the
  file is in `bin\x64_dx12`).

Never replace NVIDIA DLLs as part of installing this fix; it does not need it.

## Building from source

See `scripts/build.cmd` (portable w64devkit / GCC, no external libraries). `tools/dryrun.cpp` builds a
console tool that runs the exact same validation against a `witcher3.exe` without executing the game or
writing anything, useful for auditing a new build before deploying.

## Credits and legal

Author: Armando Galeano.

The Witcher 3 is property of CD PROJEKT RED. DLSS, Streamline and NGX are property of NVIDIA. This project
contains no files from the game, NVIDIA or any third-party mod. Source code is MIT licensed (see `LICENSE`).
