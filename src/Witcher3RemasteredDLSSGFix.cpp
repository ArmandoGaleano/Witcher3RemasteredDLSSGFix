// Witcher3RemasteredDLSSGFix  --  v0.2.0-beta
//
// Runtime-only fix for DLSS Frame Generation resolution validation in
// The Witcher 3: Wild Hunt — Remastered 5.00c (DX12), Steam build 25646871.
//
// v0.1.0-beta redirected the game's legacy DRS range calculation to its existing non-legacy path.
// That fixed resolutions such as 2560x1080, but the non-legacy path still aligned the maximum DRS
// extent down to a multiple of 4. At requested sizes such as 3413x1440 this produced 3412x1440,
// a non-zero viewport offset, and DLSS-G remained disabled.
//
// v0.2.0-beta keeps the requested maximum DRS extent exact and makes the full-resolution
// render-target descriptor use that exact extent only for the maximum DRS slot. Lower DRS slots keep
// the game's original alignment behavior, and special NGX/FSR/XeSS slots are left untouched.
//
// The plugin does not force DLSS-G on, does not disable the game's validation, and does not modify
// witcher3.exe on disk. It restores internally consistent dimensions so the game can enable DLSS-G
// through its normal path.
//
// Safety: strict signature/semantic validation, full non-legacy block validation, exact descriptor-tail
// validation, all-or-nothing runtime writes with rollback, and dry-run hook-encoding self-check.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FIX_VERSION "0.2.0-beta"
#define FIX_NAME    "Witcher3RemasteredDLSSGFix"

// ---------------------------------------------------------------------------------------------
// Signatures (verified against witcher3.exe 5.0.0.1044392: exactly one occurrence each in the whole image)
// ---------------------------------------------------------------------------------------------
static const int SIG_A[] = {
    0x40,0x57, 0x48,0x83,0xEC,0x30,
    0x80,0x3D,-1,-1,-1,-1,0x00,                     // cmp byte ptr [rip+disp32], 0   (+6)
    0x46,0x8D,0x0C,0x02,
    0xC5,0xF8,0x57,0xC0,
    0xC4,0xC1,0xFA,0x2A,0xC1,
    0xC5,0xFA,0x5C,0x0D,-1,-1,-1,-1,
    0xC5,0xF2,0x59,0x15,-1,-1,-1,-1,
    0xC5,0xF8,0x57,0xC0,
    0xC5,0xEA,0x5F,0xC8,
    0xC5,0xF2,0x5D,0x15,-1,-1,-1,-1,
    0xC5,0xEA,0x59,0x1D,-1,-1,-1,-1,
    0xC5,0xFA,0x10,0x05,-1,-1,-1,-1,
    0xC5,0xFA,0x5C,0xE3,
    0x48,0x8B,0xF9,
    0x74,0x21,                                      // je +0x21  (+0x51)
    0x33,0xC0, 0x48,0x89,0x01
};
static const size_t SIG_A_CMP = 6;
static const size_t SIG_A_JE  = 0x51;

static const int SIG_B[] = {
    0x40,0x57, 0x48,0x83,0xEC,0x30,
    0x80,0x3D,-1,-1,-1,-1,0x00,                     // cmp byte ptr [rip+disp32], 0   (+6)
    0x48,0x8B,0xF9,
    0x74,0x21,                                      // je +0x21  (+0x10)
    0x33,0xC0, 0x48,0x89,0x01, 0x48,0x89,0x41,0x08, 0x48,0x89,0x41,0x10,
    0xC5,0xFA,0x11,0x5C,0x24,0x20,
    0xE8,-1,-1,-1,-1,
    0x48,0x8B,0xC7, 0x48,0x83,0xC4,0x30, 0x5F, 0xC3
};
static const size_t SIG_B_CMP = 6;
static const size_t SIG_B_JE  = 0x10;

// The je target: non-legacy block = je + 2 + 0x21. Its full 197 bytes (build 5.00c) for each site. The only
// difference between A and B is the scale-factor register (xmm4 vs xmm3) at block offsets 24 and 86.
#define NL_LEN 197
static const uint8_t NL_A[NL_LEN] = {
0x8b,0xc2,0x45,0x8b,0xd8,0x41,0xc1,0xeb,0x02,0xc5,0xf8,0x57,0xc0,0xc4,0xe1,0xfa,0x2a,0xc0,0x41,0x8b,0xc0,0xc5,0xfa,0x59,0xcc,0xc5,0xe8,0x57,0xd2,0xc4,0xe3,0x69,0x0a,0xd1,0x02,0xc4,0x61,0xfa,0x2c,0xd2,0x41,0x83,0xc2,0x03,0x48,0x89,0x5c,0x24,0x40,0x8b,0xda,0x41,0xc1,0xea,0x02,0xc1,0xeb,0x02,0x41,0x8b,0xd3,0x44,0x8b,0xc3,0xc7,0x47,0x14,0x04,0x00,0x00,0x00,0x45,0x2b,0xc2,0xc5,0xf8,0x57,0xc0,0xc4,0xe1,0xfa,0x2a,0xc0,0xc5,0xfa,0x59,0xcc,0x41,0x8d,0x40,0x01,0xc5,0xe8,0x57,0xd2,0xc4,0xe3,0x69,0x0a,0xd1,0x02,0xc4,0x61,0xfa,0x2c,0xca,0x41,0x83,0xc1,0x03,0x41,0xc1,0xe9,0x02,0x41,0x2b,0xd1,0x8d,0x4a,0x01,0x3b,0xc1,0x8d,0x04,0x9d,0x00,0x00,0x00,0x00,0x48,0x8b,0x5c,0x24,0x40,0x89,0x07,0x41,0x0f,0x46,0xd0,0x42,0x8d,0x04,0x9d,0x00,0x00,0x00,0x00,0xff,0xc2,0x89,0x47,0x04,0x42,0x8d,0x04,0x95,0x00,0x00,0x00,0x00,0x89,0x47,0x08,0x42,0x8d,0x04,0x8d,0x00,0x00,0x00,0x00,0x89,0x47,0x0c,0xb8,0x39,0x00,0x00,0x00,0x3b,0xd0,0x0f,0x46,0xc2,0x89,0x47,0x10,0x48,0x8b,0xc7,0x48,0x83,0xc4,0x30,0x5f,0xc3 };
static const uint8_t NL_B[NL_LEN] = {
0x8b,0xc2,0x45,0x8b,0xd8,0x41,0xc1,0xeb,0x02,0xc5,0xf8,0x57,0xc0,0xc4,0xe1,0xfa,0x2a,0xc0,0x41,0x8b,0xc0,0xc5,0xfa,0x59,0xcb,0xc5,0xe8,0x57,0xd2,0xc4,0xe3,0x69,0x0a,0xd1,0x02,0xc4,0x61,0xfa,0x2c,0xd2,0x41,0x83,0xc2,0x03,0x48,0x89,0x5c,0x24,0x40,0x8b,0xda,0x41,0xc1,0xea,0x02,0xc1,0xeb,0x02,0x41,0x8b,0xd3,0x44,0x8b,0xc3,0xc7,0x47,0x14,0x04,0x00,0x00,0x00,0x45,0x2b,0xc2,0xc5,0xf8,0x57,0xc0,0xc4,0xe1,0xfa,0x2a,0xc0,0xc5,0xfa,0x59,0xcb,0x41,0x8d,0x40,0x01,0xc5,0xe8,0x57,0xd2,0xc4,0xe3,0x69,0x0a,0xd1,0x02,0xc4,0x61,0xfa,0x2c,0xca,0x41,0x83,0xc1,0x03,0x41,0xc1,0xe9,0x02,0x41,0x2b,0xd1,0x8d,0x4a,0x01,0x3b,0xc1,0x8d,0x04,0x9d,0x00,0x00,0x00,0x00,0x48,0x8b,0x5c,0x24,0x40,0x89,0x07,0x41,0x0f,0x46,0xd0,0x42,0x8d,0x04,0x9d,0x00,0x00,0x00,0x00,0xff,0xc2,0x89,0x47,0x04,0x42,0x8d,0x04,0x95,0x00,0x00,0x00,0x00,0x89,0x47,0x08,0x42,0x8d,0x04,0x8d,0x00,0x00,0x00,0x00,0x89,0x47,0x0c,0xb8,0x39,0x00,0x00,0x00,0x3b,0xd0,0x0f,0x46,0xc2,0x89,0x47,0x10,0x48,0x8b,0xc7,0x48,0x83,0xc4,0x30,0x5f,0xc3 };

// Patches inside the non-legacy block (same offsets at both sites)
struct BlockPatch { uint8_t off; uint8_t len; uint8_t orig[8]; uint8_t neu[8]; const char* what; };
static const BlockPatch BLOCK_PATCHES[4] = {
    { 0x05, 4, {0x41,0xC1,0xEB,0x02},                     {0x0F,0x1F,0x40,0x00},                     "shr r11d,2 (H/4) -> nop4" },
    { 0x37, 3, {0xC1,0xEB,0x02},                          {0x0F,0x1F,0x00},                          "shr ebx,2 (W/4) -> nop3" },
    { 0x7A, 7, {0x8D,0x04,0x9D,0x00,0x00,0x00,0x00},      {0x8B,0xC3,0x0F,0x1F,0x44,0x00,0x00},      "lea eax,[rbx*4] (maxW) -> mov eax,ebx ; nop5" },
    { 0x8C, 8, {0x42,0x8D,0x04,0x9D,0x00,0x00,0x00,0x00}, {0x41,0x8B,0xC3,0x0F,0x1F,0x44,0x00,0x00}, "lea eax,[r11*4] (maxH) -> mov eax,r11d ; nop5" },
};

// Descriptor tail in sub_1404F252E, verified from the read-only r12 static dump. The signature starts
// at RVA 0x4F263A and ends at 0x4F2677. No relocations/RIP-relative operands occur in this span.
// At entry to the hook (RVA 0x4F2646): r11d = slot k, r9d = steps-1, r8d/ecx = computed W/H,
// ebx = RT scale numerator. The original tail rounds both dimensions down by their remainders.
static const int SIG_DESC_TAIL[] = {
    0x0F,0xAF,0xC1, 0x41,0xF7,0xF1, 0x33,0xD2, 0x42,0x8D,0x0C,0x10,
    0x41,0x8B,0xC0, 0xF7,0xF3, 0x8B,0xC1, 0x44,0x2B,0xC2, 0x33,0xD2, 0xF7,0xF3,
    0x44,0x89,0x44,0x24,0x34, 0x48,0x8D,0x5F,0x08, 0x2B,0xCA, 0x89,0x4C,0x24,0x38,
    0xC5,0xFC,0x10,0x44,0x24,0x30, 0xC5,0xFC,0x11,0x07, 0xC5,0xFC,0x11,0x57,0x20,
    0xC5,0xFB,0x11,0x4F,0x40
};
static const size_t DESC_HOOK_OFF = 0x0C;
static const size_t DESC_TAIL_LEN = 29;
static const uint8_t DESC_TAIL_ORIG[DESC_TAIL_LEN] = {
    0x41,0x8B,0xC0, 0xF7,0xF3, 0x8B,0xC1, 0x44,0x2B,0xC2, 0x33,0xD2, 0xF7,0xF3,
    0x44,0x89,0x44,0x24,0x34, 0x48,0x8D,0x5F,0x08, 0x2B,0xCA, 0x89,0x4C,0x24,0x38
};

static const char     CVAR_GROUP[]  = "Rendering/DRS/Legacy";
static const char     CVAR_NAME[]   = "UseLegacy";
static const uint64_t CVAR_FLAGS    = 0x200;
static const uint64_t CVAR_DEFAULT  = 1;

// ---------------------------------------------------------------------------------------------
struct AppliedPatch { uint8_t* addr; uint8_t len; uint8_t orig[64]; };
#define MAX_PATCHES 11
static HMODULE  g_self = nullptr;
static FILE*    g_log  = nullptr;
static AppliedPatch g_patches[MAX_PATCHES];
static int      g_patchCount = 0;
static volatile LONG g_applied = 0;
static uint8_t* g_descStub = nullptr; // intentionally kept allocated until process exit

static uint64_t g_storedBase = 0;
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

static const uint8_t* ResolveRipCmp(const uint8_t* cmp) { int32_t disp; memcpy(&disp, cmp + 2, 4); return cmp + 7 + disp; }

static bool StoredStrEquals(uint64_t storedVa, const char* expect, const Section& rdata)
{
    const uint8_t* p = FromStored(storedVa);
    size_t n = strlen(expect) + 1;
    if (p < rdata.base || p + n > rdata.base + rdata.size) return false;
    return memcmp(p, expect, n) == 0;
}

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

static bool WriteBytes(uint8_t* addr, const uint8_t* value, size_t len)
{
    DWORD old = 0;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old)) { Log("VirtualProtect failed at %p (err %lu)", (void*)addr, GetLastError()); return false; }
    memcpy(addr, value, len);
    DWORD tmp = 0; VirtualProtect(addr, len, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    return true;
}

static void HexLine(char* out, size_t cap, const uint8_t* p, size_t n)
{
    size_t k = 0; out[0] = 0;
    for (size_t i = 0; i < n && k + 3 < cap; ++i) k += (size_t)snprintf(out + k, cap - k, "%s%02X", i ? " " : "", p[i]);
}

static void RevertAll()
{
    for (int i = g_patchCount - 1; i >= 0; --i) WriteBytes(g_patches[i].addr, g_patches[i].orig, g_patches[i].len);
    g_patchCount = 0;
}

static bool Queue(uint8_t* addr, const uint8_t* neu, size_t len)
{
    if (g_patchCount >= MAX_PATCHES) return false;
    AppliedPatch& ap = g_patches[g_patchCount];
    ap.addr = addr; ap.len = (uint8_t)len; memcpy(ap.orig, addr, len);
    if (!WriteBytes(addr, neu, len)) return false;
    ++g_patchCount;
    return true;
}

// Build a tiny executable trampoline for the descriptor rounding tail. The original code does:
//   width  = width  - (width  % e);
//   height = height - (height % e);
// Candidate-B performs those subtractions only when k != steps-1. The final EFLAGS for the max-slot
// path are kept equivalent to the original height SUB by using CMP without modifying ECX.
static bool BuildDescriptorStub(uint8_t* resume)
{
    static const uint8_t kTemplate[] = {
        0x41,0x8B,0xC0,                         // mov eax,r8d
        0xF7,0xF3,                              // div ebx
        0x8B,0xC1,                              // mov eax,ecx
        0x45,0x3B,0xD9,                         // cmp r11d,r9d   (k vs steps-1)
        0x74,0x03,                              // je skip width subtraction
        0x44,0x2B,0xC2,                         // sub r8d,edx
        0x33,0xD2,                              // xor edx,edx
        0xF7,0xF3,                              // div ebx
        0x44,0x89,0x44,0x24,0x34,              // mov [rsp+34h],r8d
        0x48,0x8D,0x5F,0x08,                   // lea rbx,[rdi+8]
        0x45,0x3B,0xD9,                         // cmp r11d,r9d
        0x75,0x04,                              // jne do height subtraction
        0x3B,0xCA,                              // cmp ecx,edx (same flags as original sub)
        0xEB,0x02,                              // jmp store height
        0x2B,0xCA,                              // sub ecx,edx
        0x89,0x4C,0x24,0x38,                   // mov [rsp+38h],ecx
        0xFF,0x25,0x00,0x00,0x00,0x00,         // jmp qword ptr [rip+0]
        0,0,0,0,0,0,0,0                        // absolute resume address
    };
    static const size_t kResumePtrOff = sizeof(kTemplate) - 8;

    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, sizeof(kTemplate), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mem) { Log("VirtualAlloc descriptor stub failed (err %lu)", GetLastError()); return false; }
    memcpy(mem, kTemplate, sizeof(kTemplate));
    uint64_t target = (uint64_t)(uintptr_t)resume;
    memcpy(mem + kResumePtrOff, &target, sizeof(target));

    DWORD old = 0;
    if (!VirtualProtect(mem, sizeof(kTemplate), PAGE_EXECUTE_READ, &old)) {
        Log("VirtualProtect descriptor stub failed (err %lu)", GetLastError());
        VirtualFree(mem, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), mem, sizeof(kTemplate));
    g_descStub = mem;
    Log("descriptor stub=%p size=%zu resume=%p", (void*)mem, sizeof(kTemplate), (void*)resume);
    return true;
}

static bool BuildDescriptorHookPatch(uint8_t out[DESC_TAIL_LEN], const uint8_t* stub)
{
    memset(out, 0x90, DESC_TAIL_LEN);
    out[0] = 0xFF; out[1] = 0x25;              // jmp qword ptr [rip+0]
    memset(out + 2, 0x00, 4);                  // disp32 MUST be zero
    uint64_t target = (uint64_t)(uintptr_t)stub;
    memcpy(out + 6, &target, sizeof(target));

    // Self-check the absolute-indirect jump encoding before it can ever be written.
    if (out[0] != 0xFF || out[1] != 0x25 ||
        out[2] != 0x00 || out[3] != 0x00 || out[4] != 0x00 || out[5] != 0x00)
        return false;

    uint64_t encodedTarget = 0;
    memcpy(&encodedTarget, out + 6, sizeof(encodedTarget));
    return encodedTarget == target;
}

// Core logic. exeBase = mapped image base. dryRun = validate and log only, never write.
static bool Apply(uint8_t* exeBase, bool dryRun, const char* exePathForLog)
{
    g_mappedBase = exeBase;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(exeBase + ((IMAGE_DOS_HEADER*)exeBase)->e_lfanew);
    g_storedBase = dryRun ? nt->OptionalHeader.ImageBase : (uint64_t)(uintptr_t)exeBase;

    Log("%s v%s%s", FIX_NAME, FIX_VERSION, dryRun ? " (DRY RUN: validation only, no writes)" : "");
    Log("exe=%s mappedBase=%p storedPointerBase=0x%llX", exePathForLog, (void*)exeBase, (unsigned long long)g_storedBase);
    {
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

    // Non-legacy blocks (je target) must match byte for byte, both sites, before anything is written.
    uint8_t* nlA = jeA + 2 + 0x21; uint8_t* nlB = jeB + 2 + 0x21;
    if (nlA + NL_LEN > text.base + text.size || nlB + NL_LEN > text.base + text.size) { Log("non-legacy block out of .text"); Log("unsupported game build"); return false; }
    bool okA = memcmp(nlA, NL_A, NL_LEN) == 0, okB = memcmp(nlB, NL_B, NL_LEN) == 0;
    Log("non-legacy block A at %p (RVA 0x%llX): %s ; block B at %p (RVA 0x%llX): %s",
        (void*)nlA, (unsigned long long)(nlA - exeBase), okA ? "197/197 bytes match" : "MISMATCH",
        (void*)nlB, (unsigned long long)(nlB - exeBase), okB ? "197/197 bytes match" : "MISMATCH");
    if (!okA || !okB) { Log("unsupported game build"); return false; }
    for (int s = 0; s < 2; ++s) {
        uint8_t* nl = s == 0 ? nlA : nlB;
        for (int i = 0; i < 4; ++i) {
            const BlockPatch& bp = BLOCK_PATCHES[i];
            char o[32], n[32]; HexLine(o, sizeof o, nl + bp.off, bp.len); HexLine(n, sizeof n, bp.neu, bp.len);
            bool ok = memcmp(nl + bp.off, bp.orig, bp.len) == 0;
            Log("  site %c +0x%02X (RVA 0x%llX): %s | orig [%s] %s | new [%s]", s == 0 ? 'A' : 'B', bp.off,
                (unsigned long long)(nl + bp.off - exeBase), bp.what, o, ok ? "ok" : "MISMATCH", n);
            if (!ok) { Log("unsupported game build"); return false; }
        }
    }

    uint8_t* descSig = nullptr;
    int cd = FindSig(text, SIG_DESC_TAIL, sizeof(SIG_DESC_TAIL)/sizeof(SIG_DESC_TAIL[0]), &descSig);
    Log("descriptor-tail signature: %s (%d match%s) at %p (RVA 0x%llX)", cd == 1 ? "found" : "NOT unique", cd, cd == 1 ? "" : "es",
        (void*)descSig, descSig ? (unsigned long long)(descSig - exeBase) : 0ULL);
    if (cd != 1) { Log("unsupported game build"); return false; }
    uint8_t* descTail = descSig + DESC_HOOK_OFF;
    bool descOk = memcmp(descTail, DESC_TAIL_ORIG, DESC_TAIL_LEN) == 0;
    char descOrig[128]; HexLine(descOrig, sizeof descOrig, descTail, DESC_TAIL_LEN);
    Log("descriptor rounding tail at %p (RVA 0x%llX): %s | orig [%s]", (void*)descTail,
        (unsigned long long)(descTail - exeBase), descOk ? "29/29 bytes match" : "MISMATCH", descOrig);
    if (!descOk) { Log("unsupported game build"); return false; }

    Log("current UseLegacy value in memory = %u (left untouched)", (unsigned)*gA);
    if (dryRun) {
        // Validate the hook encoder too, without allocating executable memory or writing anything.
        // The sentinel is only serialized into the local byte buffer so we can verify the exact
        // FF 25 00 00 00 00 + absolute-address encoding during dry-run.
        static const uintptr_t kDryRunStubSentinel = (uintptr_t)0x1122334455667788ULL;
        uint8_t dryHook[DESC_TAIL_LEN];
        if (!BuildDescriptorHookPatch(dryHook, (const uint8_t*)kDryRunStubSentinel)) {
            Log("DRY RUN FAILED: descriptor hook encoding self-check failed");
            return false;
        }
        char h[96];
        HexLine(h, sizeof h, dryHook, 14);
        Log("descriptor hook dry-check first14=[%s] (expected FF 25 00 00 00 00 88 77 66 55 44 33 22 11)", h);
        Log("DRY RUN result: all validations passed; would write 2 x (74->EB) + 8 exact-max block patches + 1 max-slot descriptor hook (nothing written)");
        return true;
    }

    // All validations passed: create the local descriptor stub, then apply everything. Any write failure
    // reverts all writes already made. The tiny stub allocation is deliberately kept until process exit
    // so an explicit DLL unload can never free code while another render thread is still executing it.
    if (!BuildDescriptorStub(descTail + DESC_TAIL_LEN)) { Log("patch aborted: descriptor stub allocation failed"); return false; }
    uint8_t descHook[DESC_TAIL_LEN];
    if (!BuildDescriptorHookPatch(descHook, g_descStub)) {
        Log("patch aborted: descriptor hook encoding self-check failed");
        return false;
    }
    {
        char h[96];
        HexLine(h, sizeof h, descHook, 14);
        Log("descriptor hook encoding first14=[%s] (expected FF 25 00 00 00 00 + absolute stub address)", h);
    }

    static const uint8_t JMP = 0xEB;
    g_patchCount = 0;
    bool ok = Queue(jeA, &JMP, 1) && Queue(jeB, &JMP, 1);
    for (int s = 0; ok && s < 2; ++s) {
        uint8_t* nl = s == 0 ? nlA : nlB;
        for (int i = 0; ok && i < 4; ++i) ok = Queue(nl + BLOCK_PATCHES[i].off, BLOCK_PATCHES[i].neu, BLOCK_PATCHES[i].len);
    }
    if (ok) ok = Queue(descTail, descHook, DESC_TAIL_LEN);
    if (!ok) { RevertAll(); Log("patch aborted: a write failed, everything reverted"); return false; }
    InterlockedExchange(&g_applied, 1);
    Log("patch applied: non-legacy path + exact maxW/maxH + max-slot-only RT descriptor rounding bypass; %d writes total; cvar byte untouched; hudless validation untouched",
        g_patchCount);
    return true;
}

#ifndef FIX_DRYRUN
static DWORD WINAPI InitThread(LPVOID)
{
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
    RevertAll();
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr) Restore();
    }
    return TRUE;
}
#endif
