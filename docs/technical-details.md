# Technical details

All addresses below refer to the tested build: The Witcher 3 Remastered 5.00c, Steam build 25646871,
`witcher3.exe` 5.0.0.1044392 (SHA256 `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`),
image base `0x140000000`. RVAs and assembly are given for auditability; the plugin never uses them as
criteria — it only uses byte signatures plus semantic validation.

## 1. The symptom

With DLSS Frame Generation enabled in the options (`DLSSGMode=1`, `DLSSGNumFramesToGenerate=3`) the game
generates frames at 1920x1080 but not at 2560x1080 (nor at 1680x1050, 1600x1024, 1600x900, 3620x1527).
Streamline verbose logs (`SL_LOG_LEVEL=2`) showed no error at all at the failing resolutions: the host
simply never calls `slDLSSGSetOptions` with `mode = eOn`, there is no NGX FrameGeneration feature creation
and no "interpolation state changed to enabled". Switching to 1920x1080 in the same session enables it
within a few seconds.

## 2. Vanilla behaviour: internal extent vs swapchain

In every failing case the Streamline log showed the host passing a *smaller* resolution than the swapchain:

| Requested | Swapchain | `GetOptimalSettings` DisplayWidth/Height, DLSS-RR in/out, depth, mvec | DLSS-G |
|---|---|---|---|
| 1920x1080 | 1920x1080 | 1920x1080 | ON |
| 2560x1080 | 2560x1080 | 2544x1060 | OFF |
| 1680x1050 | 1680x1050 | 1680x1008 | OFF |
| 1600x1024 | 1600x1024 | 1600x1000 | OFF |
| 1600x900  | 1600x900  | 1584x880  | OFF |
| 3620x1527 (DSR) | 3620x1527 | 3612x1512 | OFF |

The internal extent is computed before the swapchain exists and is independent of window state
(verified with Win32 probes: client area was exactly 2560x1080 when 2544x1060 was first computed).

## 3. DRS range calculation and `UseLegacy`

The viewport setup (`sub_141C10BC0`, called on init and on every resolution change) and the DRS controller
init (`sub_141BE9DA0`) call a range function that returns `{maxW, maxH, minW, minH, steps, 4}`:

```
sub_141E9DFF0(Range* out, uint W, uint H)        ; copy with factor passed in xmm3: sub_141E9E130
  factor = 0.75 - 0.25 * clamp((W+H-2000)*0.001, 0, 1)
  cmp byte ptr [rip + g_UseLegacy], 0            ; 80 3D disp32 00   (g_UseLegacy = RVA 0x5125230)
  je  non_legacy                                  ; 74 21  <- patched to EB 21
  memset(out, 0, 24); legacySolver(out, W, H, factor); return
non_legacy:
  out->maxW = W & ~3;  out->maxH = H & ~3
  out->minW = align4(ceil(W*factor)); out->minH = align4(ceil(H*factor))
  out->steps = min(min(W/4-minW/4, H/4-minH/4)+1, 57); out->align = 4
```

`g_UseLegacy` is the cvar `Rendering/DRS/Legacy` / `UseLegacy`, flags `0x200`, default `1`. Cvars with flag
`0x200` are not read from `dx12user.settings` (verified by reading the process memory: `Enable` with flag
`0x100` is applied, `UseLegacy` with `0x200` is not), so the legacy path is always taken on a stock
installation.

`out->maxW/maxH` is stored in globals, used as `DisplayWidth/DisplayHeight` for
`slDLSSGetOptimalSettings`, becomes `DLSSDOptions.outputWidth/outputHeight` (renderer state `+0xBE4/+0xBE8`
→ `sub_141ED32B0`) and is the size at which the color, depth, motion-vector, hudless and UI buffers are
created.

## 4. The legacy solver (`sub_141E9E230`)

Reconstructed from the assembly and validated against all six observed cases:

```
maxDim = max(W,H); minDim = min(W,H)
range4 = (maxDim - maxDim*factor) * 0.25
r12 = int(range4 * 0.1); cap = min(int(range4), 56); r8 = int(range4 / cap)
for step r8 .. r12:
    steps = min(int(range4 / r8), 56)
    Wp = ((maxDim>>2) - (maxDim>>2) % r8) * 4           ; maxDim rounded DOWN to a multiple of 4*r8
    for j = r8 .. 1:                                     ; integer aspect ratio r8 : j
        Hp = j*Wp / r8;  if Hp > minDim: continue
        cost = maxDim*1000/Wp + cap/steps + minDim*300/Hp
        keep the minimum cost (steps, r8, j)
maxW = (maxDim/(4*r8))*(4*r8);  maxH = 4*j*maxW/(4*r8)
```

Results: 1920x1080 → (16:9) 1920x1080 exact; 2560x1080 → (12:5) 2544x1060; 1680x1050 → (5:3) 1680x1008;
1600x1024 → (8:5) 1600x1000; 1600x900 → (9:5) 1584x880; 3620x1527 → (43:18) 3612x1512.
Predicted exact: 2560x1440, 3440x1440, 3840x1600, 3840x2160. Predicted reduced: 1280x720 → 1260x720,
1366x768 → 1344x768. These predictions are not yet confirmed in game.

## 5. Hudless/UI validation and why Frame Generation stays off

Each frame, the Streamline frame function (`sub_141B79A10`, tail at `0x141B7A643`) decides the DLSS-G mode:

```
mode = (DLSSGMode != 0) ? eOn/eDynamic : eOff
if (slDLSSGGetState().status != 0) mode = eOff
if (DLSSGUseHudless && !hudlessMatchesViewport()) mode = eOff     ; 0x141B7AB0B..0x141B7AB20
slDLSSGSetOptions(viewport, {mode, numFramesToGenerate, ...})
```

`hudlessMatchesViewport()` (`sub_141B7AF30`) returns true only if the hudless and UI textures have exactly
the viewport size (`viewport->+0x10/+0x14`, which equals the requested resolution) and the letterbox
offsets are zero. Because those textures are created at the legacy DRS maximum (2544x1060) while the
viewport/swapchain is 2560x1080, the check fails and the game keeps DLSS-G off. No Streamline, NGX or DXGI
error is involved; Streamline's own documentation requires the same equality.

## 6. Why not just force `eOn`

Forcing `slDLSSGSetOptions(eOn)` or disabling the hudless check would run Frame Generation with hudless/UI
buffers of a different size than the back buffer: Streamline would ignore them (HUD artifacts in generated
frames) and the game's own safety logic would be bypassed. Fixing the upstream range calculation makes every
buffer match the swapchain, after which the game enables DLSS-G by itself through its normal path — the
same path it uses at 1920x1080.

## 7. The runtime patch

Two bytes are changed in process memory, each `74 21` (`je +0x21`) → `EB 21` (`jmp +0x21`):

| Site | Function | `je` RVA (tested build) | Signature |
|---|---|---|---|
| A | `sub_141E9DFF0` (range, computes factor) | `0x1E9E041` | SIG_A, 88 bytes, `je` at +0x51 |
| B | `sub_141E9E130` (range, factor in xmm3) | `0x1E9E140` | SIG_B, 51 bytes, `je` at +0x10 |

SIG_A (`??` = wildcard, only RIP-relative displacements):

```
40 57 48 83 EC 30 80 3D ?? ?? ?? ?? 00 46 8D 0C 02 C5 F8 57 C0 C4 C1 FA 2A C1 C5 FA 5C 0D ?? ?? ?? ??
C5 F2 59 15 ?? ?? ?? ?? C5 F8 57 C0 C5 EA 5F C8 C5 F2 5D 15 ?? ?? ?? ?? C5 EA 59 1D ?? ?? ?? ??
C5 FA 10 05 ?? ?? ?? ?? C5 FA 5C E3 48 8B F9 74 21 33 C0 48 89 01
```

SIG_B:

```
40 57 48 83 EC 30 80 3D ?? ?? ?? ?? 00 48 8B F9 74 21 33 C0 48 89 01 48 89 41 08 48 89 41 10
C5 FA 11 5C 24 20 E8 ?? ?? ?? ?? 48 8B C7 48 83 C4 30 5F C3
```

The third `UseLegacy` reference (DRS controller, `sub_141E9F810` at RVA `0x1E9F935`) is intentionally not
patched in this version: it only selects which dynamic-resolution controller runs, and the in-game UI keeps
DRS disabled while DLSS is active.

## 8. Validation (all-or-nothing)

1. Main module PE headers: MZ/PE, x64; sections `.text`, `.data`, `.rdata`.
2. SIG_A and SIG_B each found exactly once in `.text` (the search stops at the second hit).
3. The `cmp byte [rip+disp32],0` of both sites resolves to the same address inside `.data`.
4. Exactly one 8-byte slot in `.data` holds that address; the cvar table entry around it must read
   `[slot-24]` → `"Rendering/DRS/Legacy"`, `[slot-16]` → `"UseLegacy"` (strings inside `.rdata`),
   `[slot-8] == 0x200`, `[slot+8] == 1`.
5. Original opcodes `74 21` present at both sites.
6. Write site A, then site B; if B fails, A is reverted. Only then the log says `patch applied`.

Any failure → log line `unsupported game build`, nothing written, game runs unmodified.

## 9. Loader lock and timing

`DllMain` only calls `DisableThreadLibraryCalls` and `CreateThread`. The worker thread starts after the
loader releases the loader lock; it performs the PE inspection, scanning, logging, `VirtualProtect`, the two
writes and `FlushInstructionCache`. The first DRS range calculation happens about 10 s after process start
(viewport creation), the thread finishes within milliseconds. Each write is a single byte (atomic) and both
`74 21` and `EB 21` are valid 2-byte instructions, so there is no inconsistent intermediate state.

## 10. Rollback

Original bytes are kept. On `DLL_PROCESS_DETACH` with `lpReserved == NULL` (explicit `FreeLibrary`) both
bytes are restored; nothing is done on process termination. Nothing is ever written to disk.

## 11. Audit tool

`tools/dryrun.cpp` includes the same source with `FIX_DRYRUN` and runs `Apply(..., dryRun=true)` on a
`witcher3.exe` mapped with `LOAD_LIBRARY_AS_IMAGE_RESOURCE` (no game code executes). It prints the same log
lines as the plugin and ends with `would write EB at both sites` or `unsupported game build`.
