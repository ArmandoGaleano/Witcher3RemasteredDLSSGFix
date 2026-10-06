# Technical details — v0.2.0-beta

All addresses and byte-level observations refer to the tested build:

- The Witcher 3 Remastered 5.00c
- Steam build 25646871
- `witcher3.exe` 5.0.0.1044392
- SHA256 `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`
- image base `0x140000000`

The plugin validates signatures and semantics at runtime; it does not rely only on fixed RVAs.

## 1. Original problem fixed by v0.1.0-beta

The stock game normally uses the `Rendering/DRS/Legacy / UseLegacy` path. Its legacy range solver can
select an internal maximum smaller than the requested resolution.

Observed examples:

| Requested | Legacy maximum |
|---|---|
| 2560x1080 | 2544x1060 |
| 1680x1050 | 1680x1008 |
| 1600x1024 | 1600x1000 |
| 1600x900 | 1584x880 |
| 3620x1527 | 3612x1512 |

That mismatch feeds the DLSS/RR resource sizes and prevents the game's Hudless/UI validation from
accepting Frame Generation.

v0.1.0-beta changes both `UseLegacy` range decisions to select the game's existing non-legacy range path.

## 2. The remaining v0.1.0 limitation

The non-legacy path itself aligns its maximum extent down to a multiple of 4:

```text
maxW = W & ~3
maxH = H & ~3
```

For a requested size that is not divisible by 4:

```text
3413x1440 requested
-> non-legacy max = 3412x1440
```

Runtime analysis of the viewport setup showed that the viewport follows the DRS maximum and the
difference from the requested size becomes a letterbox offset:

```text
viewport = 3412x1440
offsets  = 1,0
```

`hudlessMatchesViewport()` first requires the viewport offsets to be zero and then validates the relevant
Hudless/UI resources. Therefore the 1-pixel offset is enough to keep DLSS-G off.

This explains why v0.1.0-beta fixed 2560x1080 but did not fix 3413x1440.

## 3. Why making only the logical maximum exact was insufficient

An intermediate candidate changed the non-legacy maximum from 3412x1440 to 3413x1440.

That removed the viewport offset and allowed the game to create the DLSS-G feature, but the
full-resolution render-target descriptor path still aligned its physical output size down to 3412 pixels.

The result was:

```text
DLSS-RR logical output: 3413x1440
physical full-res RT:   3412x1440
```

NGX rejected RR evaluation with `NVSDK_NGX_Result_FAIL_InvalidParameter`, producing a black 3D scene.

That candidate was rejected.

## 4. v0.2.0-beta design

v0.2.0-beta fixes both sides of the contract.

### 4.1 Exact maximum DRS extent

The two non-legacy range blocks are validated in full and then narrowly adjusted so that the maximum
DRS slot retains the requested width/height instead of truncating them to a multiple of 4.

The lower/minimum calculations keep their original alignment behavior.

### 4.2 Exact descriptor only for the maximum DRS slot

The render-target descriptor tail contains the final modulo alignment used for DRS slots.

At that point the function still has both:

- the current slot index `k`;
- `steps - 1`, which identifies the maximum DRS slot.

The v0.2 correction preserves the original modulo alignment for `k < steps - 1` and skips that final
round-down only when `k == steps - 1`.

Slots at or above `steps` take a different path before this tail and are not modified by this correction.

For 3413x1440 this gives:

```text
maximum slot: 3413x1440   (exact)
half/lower slots: original aligned sizes
special NGX/FSR/XeSS slots: original path
```

This keeps the full-resolution target consistent with the exact DRS maximum without broadening the
change to every render target.

## 5. Runtime patch set

The supported build receives one validated patch set:

- 2 legacy-DRS branch changes (the original v0.1 behavior);
- 8 byte-block changes across the two validated non-legacy range blocks to preserve the exact maximum;
- 1 validated descriptor-tail redirection used to apply the maximum-slot-only rule.

The plugin reports 11 runtime write operations when the complete patch set is installed.

All writes are in process memory only.

## 6. Descriptor hook and encoding validation

The descriptor-tail redirection uses an x64 absolute indirect jump encoded as:

```text
FF 25 00 00 00 00 <8-byte absolute target>
```

A pre-release candidate accidentally left the four RIP-displacement bytes as `90 90 90 90`, causing an
immediate access violation at the hook site before the trampoline executed.

The corrected implementation explicitly writes `00 00 00 00`, verifies the encoded target and aborts if
the encoding self-check fails.

The dry-run path additionally validates the encoder using the sentinel target
`0x1122334455667788`, which must serialize as:

```text
FF 25 00 00 00 00 88 77 66 55 44 33 22 11
```

This exact dry-run check passed before the successful runtime validation.

## 7. Runtime validation / fail-safe checks

Before applying anything, the implementation validates the expected executable and patch context,
including:

1. x64 PE/module layout.
2. Unique occurrence of both legacy-DRS signatures.
3. Both sites resolving to the same `UseLegacy` global.
4. Expected cvar metadata (`Rendering/DRS/Legacy`, `UseLegacy`, flags/default).
5. Full expected 197-byte non-legacy blocks at both range functions.
6. Original bytes for every exact-maximum block change.
7. Unique descriptor-tail signature.
8. Exact original descriptor-tail bytes before redirection.
9. Correct jump encoding and absolute target.
10. Allocation/build of the runtime stub before the patch set is committed.

If validation fails, the patch is not considered supported.

## 8. 3413x1440 evidence

The fully instrumented successful run produced:

```text
requested/swapchain : 3413x1440
DRS max             : 3413x1440
DRS min             : 1708x720
steps / align       : 57 / 4
render              : 2275x960
viewport            : 3413x1440
offsets             : 0,0
full-resolution RTs : 3413x1440
DLSS-RR             : 2275x960 -> 3413x1440
RR InvalidParameter : 0
DLSS-G feature      : created
interpolation state : enabled / eOn
MultiFrameCount     : 3
HasHudless          : 1
HasAlphaUI          : 1
```

The lower-size RT classes remained on their aligned dimensions.

## 9. Regression evidence

After the 3413x1440 instrumented run:

- 3620x1527: Frame Generation working after cold start.
- 2560x1080: Frame Generation working after cold start.
- 3840x1620: Frame Generation working after an in-session resolution switch.
- 1920x1080: Frame Generation working after an in-session resolution switch.
- All listed modes were also switched through in one session without visible corruption or a crash.

## 10. Focus-loss observation

One `DXGI_ERROR_INVALID_CALL` was logged during a fullscreen-exclusive focus transition. Streamline had
already disabled DLSS-G because the window was not focused. Frame Generation returned to `eOn` after focus
returned and the error did not repeat.

This is tracked as an observation rather than a release blocker.

## 11. What the fix deliberately does not change

The plugin does not:

- force `eOn`;
- patch `hudlessMatchesViewport()`;
- disable any DLSS-G validation;
- replace Streamline or NVIDIA libraries;
- change graphics settings or cvars;
- patch the executable on disk.

The intent is to restore a consistent resolution contract so the game can make its normal DLSS-G decision.
