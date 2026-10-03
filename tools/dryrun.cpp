// Dry-run audit tool: maps witcher3.exe as an IMAGE RESOURCE (sections laid out at their RVAs, no code
// executed, no relocation, no DllMain) and runs exactly the same validation as the ASI, with writes disabled.
// Usage: dryrun.exe "<path to witcher3.exe>" "<log path>"
#define FIX_DRYRUN
#include "../src/Witcher3RemasteredDLSSGFix.cpp"

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: dryrun.exe <witcher3.exe> <log>\n"); return 2; }
    wchar_t wpath[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wpath, MAX_PATH);
    HMODULE h = LoadLibraryExW(wpath, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (!h) { fprintf(stderr, "LoadLibraryEx failed: %lu\n", GetLastError()); return 3; }
    uint8_t* base = (uint8_t*)((uintptr_t)h & ~(uintptr_t)3); // image-resource handles have low bits set
    LogOpen(argv[2]);
    bool ok = Apply(base, true, argv[1]);
    LogClose();
    FreeLibrary(h);
    printf("dry run %s\n", ok ? "PASSED (all validations ok, nothing written)" : "FAILED / unsupported build (nothing written)");
    return ok ? 0 : 1;
}
