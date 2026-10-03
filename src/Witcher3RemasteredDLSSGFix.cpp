// Witcher3RemasteredDLSSGFix.asi  (v1.0.0)
//
// Purpose: make the two DRS "range" functions of witcher3.exe (The Witcher 3 Remastered 5.00c)
// take the NON-legacy branch, which is the logical equivalent of the cvar
// Rendering/DRS/Legacy UseLegacy=false (the settings loader ignores that cvar: flag 0x200).
// With the legacy solver active, the maximum internal resolution for 2560x1080 becomes 2544x1060,
// so the hudless/UI buffers differ from the viewport/backbuffer and the game never enables DLSS-G.
//
// What it does: on a worker thread (outside the loader lock) it pattern-scans the .text section of
// witcher3.exe for the two decision sites, validates them semantically (both `cmp byte [rip+X],0`
// must point at the same global and that global must be the cvar table entry
// "Rendering/DRS/Legacy" / "UseLegacy" / flags 0x200 / default 1), validates the original opcode
// (`74 21` = je +0x21) and only then changes BOTH to `EB 21` (jmp +0x21). All-or-nothing.
// Nothing on disk is modified. No hooks, no trampolines, no external dependencies.
//
// What it does NOT do: never calls Streamline, never touches DLSSGUseHudless/DLSSGMode/Reflex/NGX,
// never touches saves or settings, never patches the DRS controller, never logs per frame.
//
// Build: see build.cmd (w64devkit, g++ -O2, static). Dry-run audit tool: test\dryrun.cpp includes
// this file with FIX_DRYRUN defined and runs Apply(exe, true) on witcher3.exe mapped as an image
// resource (no code of the game is executed, nothing is written).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FIX_VERSION "1.0.0"
#define FIX_NAME    "Witcher3RemasteredDLSSGFix"

// ---------------------------------------------------------------------------------------------
// Signatures (verified against witcher3.exe 5.0.0.1044392: exactly one occurrence each in the whole image)
// -1 = wildcard (RIP-relative displacements / call target). The `je` offset inside each signature is
// additionally checked against the original opcode bytes 74 21 before anything is written.
// ---------------------------------------------------------------------------------------------
// Site A: DRS range function that computes the scale factor itself (sub_141E9DFF0 in build 5.00c)
static const int SIG_A[] = {
    0x40,0x57, 0x48,0x83,0xEC,0x30,                 // push rdi ; sub rsp,0x30
    0x80,0x3D,-1,-1,-1,-1,0x00,                     // cmp byte ptr [rip+disp32], 0      (+6, disp at +8)
    0x46,0x8D,0x0C,0x02,                            // lea r9d,[rdx+r8]
    0xC5,0xF8,0x57,0xC0,                            // vxorps xmm0,xmm0,xmm0
    0xC4,0xC1,0xFA,0x2A,0xC1,                       // vcvtsi2ss xmm0,xmm0,r9
    0xC5,0xFA,0x5C,0x0D,-1,-1,-1,-1,                // vsubss xmm1,xmm0,[rip+..]        (2000.0f)
    0xC5,0xF2,0x59,0x15,-1,-1,-1,-1,                // vmulss xmm2,xmm1,[rip+..]        (0.001f)
    0xC5,0xF8,0x57,0xC0,                            // vxorps xmm0,xmm0,xmm0
    0xC5,0xEA,0x5F,0xC8,                            // vmaxss xmm1,xmm2,xmm0
    0xC5,0xF2,0x5D,0x15,-1,-1,-1,-1,                // vminss xmm2,xmm1,[rip+..]        (1.0f)
    0xC5,0xEA,0x59,0x1D,-1,-1,-1,-1,                // vmulss xmm3,xmm2,[rip+..]        (0.25f)
    0xC5,0xFA,0x10,0x05,-1,-1,-1,-1,                // vmovss xmm0,[rip+..]             (0.75f)
    0xC5,0xFA,0x5C,0xE3,                            // vsubss xmm4,xmm0,xmm3
    0x48,0x8B,0xF9,                                 // mov rdi,rcx
    0x74,0x21,                                      // je +0x21  <-- patched to EB 21   (+0x51)
    0x33,0xC0, 0x48,0x89,0x01                       // xor eax,eax ; mov [rcx],rax
};
static const size_t SIG_A_CMP = 6;
static const size_t SIG_A_JE  = 0x51;

// Site B: copy of the range function that receives the scale factor in xmm3 (sub_141E9E130 in build 5.00c)
static const int SIG_B[] = {
    0x40,0x57, 0x48,0x83,0xEC,0x30,                 // push rdi ; sub rsp,0x30
    0x80,0x3D,-1,-1,-1,-1,0x00,                     // cmp byte ptr [rip+disp32], 0      (+6, disp at +8)
    0x48,0x8B,0xF9,                                 // mov rdi,rcx
    0x74,0x21,                                      // je +0x21  <-- patched to EB 21   (+0x10)
    0x33,0xC0, 0x48,0x89,0x01, 0x48,0x89,0x41,0x08, 0x48,0x89,0x41,0x10,   // zero the 24-byte out struct
    0xC5,0xFA,0x11,0x5C,0x24,0x20,                  // vmovss [rsp+0x20],xmm3
    0xE8,-1,-1,-1,-1,                               // call legacySolver
    0x48,0x8B,0xC7, 0x48,0x83,0xC4,0x30, 0x5F, 0xC3 // mov rax,rdi ; add rsp,0x30 ; pop rdi ; ret
};
static const size_t SIG_B_CMP = 6;
static const size_t SIG_B_JE  = 0x10;

// Expected cvar table entry layout (verified): [slot-24]=group name ptr, [slot-16]=name ptr,
// [slot-8]=flags, [slot]=pointer to the variable, [slot+8]=default value.
static const char     CVAR_GROUP[]  = "Rendering/DRS/Legacy";
static const char     CVAR_NAME[]   = "UseLegacy";
static const uint64_t CVAR_FLAGS    = 0x200;
static const uint64_t CVAR_DEFAULT  = 1;

// ---------------------------------------------------------------------------------------------
static HMODULE  g_self = nullptr;
static FILE*    g_log  = nullptr;
static uint8_t* g_patchAddr[2] = { nullptr, nullptr };
static uint8_t  g_patchOrig[2] = { 0, 0 };
static volatile LONG g_applied = 0;

// Address translation: in the real process pointers stored in .data are already relocated by the loader.
// In the dry-run the image is mapped unrelocated, so stored pointers are preferred-base VAs.
static uint64_t g_storedBase = 0;   // base the stored pointers refer to
static uint8_t* g_mappedBase = nullptr;
static uint8_t* FromStored(uint64_t va) { return g_mappedBase + (va - g_storedBase); }
static uint64_t ToStored(const uint8_t* p) { return g_storedBase + (uint64_t)(p - g_mappedBase); }

static void LogOpen(const char* explicitPath)
{
    char path[MAX_PATH];
    if (explicitPath) { strncpy(path, explicitPath, MAX_PATH - 1); path[MAX_PATH - 1] = 0; }
    else {
        DWORD n = GetModuleFileNameA(g_self, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
        char* dot = strrchr(path, '.'); char* sl = strrchr(path, '\\');
        if (dot && (!sl || dot > sl)) *dot = 0;
        strncat(path, ".log", MAX_PATH - strlen(path) - 1);
    }
    g_log = fopen(path, "w");
}
static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}
static void LogClose() { if (g_log) { fclose(g_log); g_log = nullptr; } }

struct Section { uint8_t* base; size_t size; };

static bool GetSection(uint8_t* mod, const char* name, Section& out)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)mod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(mod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char n[9] = {0}; memcpy(n, sec[i].Name, 8);
        if (strcmp(n, name) == 0) {
            out.base = mod + sec[i].VirtualAddress;
            out.size = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            return true;
        }
    }
    return false;
}

// Returns number of matches (stops counting at 2); first match address in *first.
static int FindSig(const Section& s, const int* sig, size_t len, uint8_t** first)
{
    int count = 0; *first = nullptr;
    if (s.size < len) return 0;
    const uint8_t* p = s.base; const uint8_t* end = s.base + s.size - len;
    for (; p <= end; ++p) {
        if (sig[0] != -1 && p[0] != (uint8_t)sig[0]) continue;
        size_t k = 1;
        for (; k < len; ++k) { if (sig[k] != -1 && p[k] != (uint8_t)sig[k]) break; }
        if (k == len) { if (count == 0) *first = (uint8_t*)p; if (++count >= 2) return count; }
    }
    return count;
}

static const uint8_t* ResolveRipCmp(const uint8_t* cmp) // 80 3D disp32 imm8 -> target = cmp+7+disp
{
    int32_t disp; memcpy(&disp, cmp + 2, 4);
    return cmp + 7 + disp;
}

static bool StoredStrEquals(uint64_t storedVa, const char* expect, const Section& rdata)
{
    const uint8_t* p = FromStored(storedVa);
    size_t n = strlen(expect) + 1;
    if (p < rdata.base || p + n > rdata.base + rdata.size) return false;
    return memcmp(p, expect, n) == 0;
}

// Validate the cvar table entry whose variable pointer equals `var`. Exactly one slot must exist in .data.
static bool ValidateCvar(const Section& data, const Section& rdata, const uint8_t* var, uint8_t** slotOut)
{
    uint64_t want = ToStored(var); int count = 0; uint8_t* slot = nullptr;
    for (uint8_t* p = data.base; p + 8 <= data.base + data.size; p += 8) {
        uint64_t q; memcpy(&q, p, 8);
        if (q == want) { if (count == 0) slot = p; if (++count >= 2) break; }
    }
    if (count != 1 || slot < data.base + 24 || slot + 16 > data.base + data.size) { Log("cvar slot count=%d (expected 1)", count); return false; }
    uint64_t grp, nm, flags, def;
    memcpy(&grp, slot - 24, 8); memcpy(&nm, slot - 16, 8); memcpy(&flags, slot - 8, 8); memcpy(&def, slot + 8, 8);
    bool okGrp = StoredStrEquals(grp, CVAR_GROUP, rdata);
    bool okNm  = StoredStrEquals(nm, CVAR_NAME, rdata);
    Log("cvar slot=%p group='%s'(%s) name='%s'(%s) flags=0x%llX default=%llu",
        (void*)slot, CVAR_GROUP, okGrp ? "ok" : "MISMATCH", CVAR_NAME, okNm ? "ok" : "MISMATCH",
        (unsigned long long)flags, (unsigned long long)def);
    *slotOut = slot;
    return okGrp && okNm && flags == CVAR_FLAGS && def == CVAR_DEFAULT;
}

static bool WriteByte(uint8_t* addr, uint8_t value)
{
    DWORD old = 0;
    if (!VirtualProtect(addr, 1, PAGE_EXECUTE_READWRITE, &old)) { Log("VirtualProtect failed at %p (err %lu)", (void*)addr, GetLastError()); return false; }
    *addr = value;
    DWORD tmp = 0; VirtualProtect(addr, 1, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), addr, 1);
    return true;
}

// Core logic. exeBase = mapped image base. dryRun = validate and log only, never write.
// Returns true when everything validated (and, if !dryRun, both bytes were written).
static bool Apply(uint8_t* exeBase, bool dryRun, const char* exePathForLog)
{
    g_mappedBase = exeBase;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(exeBase + ((IMAGE_DOS_HEADER*)exeBase)->e_lfanew);
    g_storedBase = dryRun ? nt->OptionalHeader.ImageBase : (uint64_t)(uintptr_t)exeBase;

    Log("%s v%s%s", FIX_NAME, FIX_VERSION, dryRun ? " (DRY RUN: validation only, no writes)" : "");
    Log("exe=%s mappedBase=%p storedPointerBase=0x%llX", exePathForLog, (void*)exeBase, (unsigned long long)g_storedBase);
    {   // informational only
        DWORD h = 0; DWORD sz = GetFileVersionInfoSizeA(exePathForLog, &h);
        if (sz) {
            void* buf = HeapAlloc(GetProcessHeap(), 0, sz);
            if (buf && GetFileVersionInfoA(exePathForLog, 0, sz, buf)) {
                VS_FIXEDFILEINFO* ffi = nullptr; UINT len = 0;
                if (VerQueryValueA(buf, "\\", (void**)&ffi, &len) && ffi)
                    Log("exe FileVersion=%u.%u.%u.%u", HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS), HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
            }
            if (buf) HeapFree(GetProcessHeap(), 0, buf);
        }
    }

    Section text, data, rdata;
    if (!GetSection(exeBase, ".text", text) || !GetSection(exeBase, ".data", data) || !GetSection(exeBase, ".rdata", rdata)) {
        Log("PE sections not found"); Log("unsupported game build"); return false;
    }
    Log(".text=%p size=0x%zX .data=%p size=0x%zX .rdata=%p size=0x%zX", (void*)text.base, text.size, (void*)data.base, data.size, (void*)rdata.base, rdata.size);

    uint8_t *a = nullptr, *b = nullptr;
    int ca = FindSig(text, SIG_A, sizeof(SIG_A)/sizeof(SIG_A[0]), &a);
    int cb = FindSig(text, SIG_B, sizeof(SIG_B)/sizeof(SIG_B[0]), &b);
    Log("signature A: %s (%d match%s) at %p (RVA 0x%llX)", ca == 1 ? "found" : "NOT unique", ca, ca == 1 ? "" : "es", (void*)a, a ? (unsigned long long)(a - exeBase) : 0ULL);
    Log("signature B: %s (%d match%s) at %p (RVA 0x%llX)", cb == 1 ? "found" : "NOT unique", cb, cb == 1 ? "" : "es", (void*)b, b ? (unsigned long long)(b - exeBase) : 0ULL);
    if (ca != 1 || cb != 1) { Log("unsupported game build"); return false; }

    const uint8_t* gA = ResolveRipCmp(a + SIG_A_CMP);
    const uint8_t* gB = ResolveRipCmp(b + SIG_B_CMP);
    Log("cmp targets: A=%p B=%p (RVA 0x%llX) %s", (const void*)gA, (const void*)gB, (unsigned long long)(gA - exeBase), gA == gB ? "same global" : "DIFFERENT");
    if (gA != gB || gA < data.base || gA >= data.base + data.size) { Log("unsupported game build"); return false; }

    uint8_t* slot = nullptr;
    if (!ValidateCvar(data, rdata, gA, &slot)) { Log("unsupported game build"); return false; }

    uint8_t* jeA = a + SIG_A_JE; uint8_t* jeB = b + SIG_B_JE;
    Log("site A je at %p (RVA 0x%llX) bytes=%02X %02X ; site B je at %p (RVA 0x%llX) bytes=%02X %02X",
        (void*)jeA, (unsigned long long)(jeA - exeBase), jeA[0], jeA[1], (void*)jeB, (unsigned long long)(jeB - exeBase), jeB[0], jeB[1]);
    if (!(jeA[0] == 0x74 && jeA[1] == 0x21 && jeB[0] == 0x74 && jeB[1] == 0x21)) { Log("original opcode mismatch"); Log("unsupported game build"); return false; }

    Log("current UseLegacy value in memory = %u (left untouched)", (unsigned)*gA);
    if (dryRun) { Log("DRY RUN result: all validations passed; would write EB at both sites (A RVA 0x%llX, B RVA 0x%llX)", (unsigned long long)(jeA - exeBase), (unsigned long long)(jeB - exeBase)); return true; }

    // All validations passed: apply both (all-or-nothing; if the second write fails, revert the first).
    g_patchAddr[0] = jeA; g_patchOrig[0] = jeA[0];
    g_patchAddr[1] = jeB; g_patchOrig[1] = jeB[0];
    if (!WriteByte(jeA, 0xEB)) { Log("patch aborted (site A write failed)"); g_patchAddr[0] = g_patchAddr[1] = nullptr; return false; }
    if (!WriteByte(jeB, 0xEB)) { WriteByte(jeA, g_patchOrig[0]); Log("patch aborted (site B write failed, site A reverted)"); g_patchAddr[0] = g_patchAddr[1] = nullptr; return false; }
    InterlockedExchange(&g_applied, 1);
    Log("patch applied: A %02X 21 -> %02X %02X ; B %02X 21 -> %02X %02X (je->jmp: non-legacy DRS range path, equivalent to UseLegacy=false; cvar byte untouched)",
        g_patchOrig[0], jeA[0], jeA[1], g_patchOrig[1], jeB[0], jeB[1]);
    return true;
}

#ifndef FIX_DRYRUN
static DWORD WINAPI InitThread(LPVOID)
{
    // Executes only after the loader released the loader lock (our DllMain already returned).
    LogOpen(nullptr);
    HMODULE exe = GetModuleHandleW(nullptr);
    char exePath[MAX_PATH] = {0}; GetModuleFileNameA(exe, exePath, MAX_PATH);
    Apply((uint8_t*)exe, false, exePath);
    LogClose();
    return 0;
}

static void Restore()
{
    if (InterlockedCompareExchange(&g_applied, 0, 1) != 1) return;
    for (int i = 0; i < 2; ++i) if (g_patchAddr[i]) WriteByte(g_patchAddr[i], g_patchOrig[i]);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr); // only this inside DllMain
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr) Restore(); // explicit FreeLibrary only; nothing on process termination
    }
    return TRUE;
}
#endif
