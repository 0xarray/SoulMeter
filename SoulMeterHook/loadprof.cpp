#include "loadprof.h"

#include <windows.h>
#include <MinHook.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr size_t kMaxFiles = 512;
constexpr size_t kHandleSlots = 4096;   // power of two
constexpr size_t kPathMax = 260;

// <=4K, <=16K, <=64K, <=256K, <=1M, >1M
constexpr int kBuckets = 6;

struct FileStat {
    char path[kPathMax];
    uint64_t opens;
    uint64_t reads;
    uint64_t bytes;
    uint64_t readTicks;      // QPC in ReadFile
    uint64_t seeks;
    uint64_t seekBytes;      // total absolute distance skipped
    uint64_t backSeeks;      // seeks to a lower offset: read-ahead cannot help these
    uint64_t sizeHist[kBuckets];
    uint64_t maps;           // MapViewOfFile calls: these bypass ReadFile entirely
    uint64_t mapBytes;
    uint64_t firstTick;
    uint64_t lastTick;
    int64_t  nextOffset;     // -1 when unknown
};

struct HandleEnt {
    HANDLE h;
    int fileIdx;
};

FileStat g_files[kMaxFiles];
size_t g_fileCount = 0;
HandleEnt g_handles[kHandleSlots];

CRITICAL_SECTION g_cs;
bool g_csReady = false;
volatile LONG g_installed = 0;

uint64_t g_qpf = 1;
uint64_t g_startTick = 0;

char g_reportPath[MAX_PATH] = { 0 };

// --- originals -------------------------------------------------------------

typedef HANDLE(WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef BOOL(WINAPI* ReadFileFn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef DWORD(WINAPI* SetFilePointerFn)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL(WINAPI* SetFilePointerExFn)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
typedef HANDLE(WINAPI* CreateFileMappingAFn)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD,
                                             LPCSTR);
typedef HANDLE(WINAPI* CreateFileMappingWFn)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD,
                                             LPCWSTR);
typedef LPVOID(WINAPI* MapViewOfFileFn)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);

CreateFileWFn OrigCreateFileW = nullptr;
CreateFileAFn OrigCreateFileA = nullptr;
ReadFileFn OrigReadFile = nullptr;
SetFilePointerFn OrigSetFilePointer = nullptr;
SetFilePointerExFn OrigSetFilePointerEx = nullptr;
CreateFileMappingAFn OrigCreateFileMappingA = nullptr;
CreateFileMappingWFn OrigCreateFileMappingW = nullptr;
MapViewOfFileFn OrigMapViewOfFile = nullptr;

// --- helpers ---------------------------------------------------------------

inline uint64_t Now() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (uint64_t)li.QuadPart;
}

inline double MsSince(uint64_t t) { return (double)t * 1000.0 / (double)g_qpf; }

// Only the last two path components are kept: the archives all live in one
// directory and the full path buries the interesting part.
void ShortenPath(const char* in, char* out, size_t outCap) {
    size_t len = strlen(in);
    size_t seps = 0;
    size_t start = 0;
    for (size_t i = len; i > 0; i--) {
        if (in[i - 1] == '\\' || in[i - 1] == '/') {
            if (++seps == 2) {
                start = i;
                break;
            }
        }
    }
    size_t n = len - start;
    if (n >= outCap)
        n = outCap - 1;
    memcpy(out, in + start, n);
    out[n] = 0;
}

int BucketOf(DWORD n) {
    if (n <= 4 * 1024) return 0;
    if (n <= 16 * 1024) return 1;
    if (n <= 64 * 1024) return 2;
    if (n <= 256 * 1024) return 3;
    if (n <= 1024 * 1024) return 4;
    return 5;
}

inline size_t HandleSlot(HANDLE h) {
    return ((size_t)(uintptr_t)h >> 2) & (kHandleSlots - 1);
}

// Caller holds the lock.
int FindOrAddFile(const char* shortPath) {
    for (size_t i = 0; i < g_fileCount; i++) {
        if (strcmp(g_files[i].path, shortPath) == 0)
            return (int)i;
    }
    if (g_fileCount >= kMaxFiles)
        return -1;
    int idx = (int)g_fileCount++;
    FileStat& f = g_files[idx];
    memset(&f, 0, sizeof(f));
    strncpy_s(f.path, shortPath, _TRUNCATE);
    f.nextOffset = -1;
    f.firstTick = Now();
    return idx;
}

// Caller holds the lock.
void BindHandle(HANDLE h, int fileIdx) {
    // A handle value is only reused after a close, so overwriting on open keeps
    // the table correct without hooking CloseHandle.
    size_t slot = HandleSlot(h);
    for (size_t probe = 0; probe < 8; probe++) {
        HandleEnt& e = g_handles[(slot + probe) & (kHandleSlots - 1)];
        if (e.h == h || e.h == nullptr) {
            e.h = h;
            e.fileIdx = fileIdx;
            return;
        }
    }
    HandleEnt& e = g_handles[slot];
    e.h = h;
    e.fileIdx = fileIdx;
}

// Caller holds the lock.
int LookupHandle(HANDLE h) {
    size_t slot = HandleSlot(h);
    for (size_t probe = 0; probe < 8; probe++) {
        const HandleEnt& e = g_handles[(slot + probe) & (kHandleSlots - 1)];
        if (e.h == h)
            return e.fileIdx;
        if (e.h == nullptr)
            return -1;
    }
    return -1;
}

// Data files only. Hooking every handle in the process would bury the signal in
// GameGuard's own scanning and in log writes.
bool IsInteresting(const char* path) {
    size_t len = strlen(path);
    if (len < 3)
        return false;
    const char* ext = path + len;
    while (ext > path && *(ext - 1) != '.' && *(ext - 1) != '\\' && *(ext - 1) != '/')
        ext--;
    return _stricmp(ext, "v") == 0 || _stricmp(ext, "vArc") == 0 ||
           _stricmp(ext, "vPlugin") == 0 || _stricmp(ext, "swf") == 0 ||
           _stricmp(ext, "gfx") == 0 || _stricmp(ext, "dat") == 0 ||
           _stricmp(ext, "vResources") == 0;
}

void NoteOpen(HANDLE h, const char* path) {
    if (h == INVALID_HANDLE_VALUE || !path || !IsInteresting(path))
        return;

    char shortPath[kPathMax];
    ShortenPath(path, shortPath, sizeof(shortPath));

    EnterCriticalSection(&g_cs);
    int idx = FindOrAddFile(shortPath);
    if (idx >= 0) {
        g_files[idx].opens++;
        g_files[idx].nextOffset = -1;
        BindHandle(h, idx);
    }
    LeaveCriticalSection(&g_cs);
}

// --- hooks -----------------------------------------------------------------

HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = OrigCreateFileW(name, access, share, sa, disp, flags, tmpl);
    __try {
        if (h != INVALID_HANDLE_VALUE && name) {
            char narrow[kPathMax];
            int n = WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr,
                                        nullptr);
            if (n > 0)
                NoteOpen(h, narrow);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return h;
}

HANDLE WINAPI HookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                              DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = OrigCreateFileA(name, access, share, sa, disp, flags, tmpl);
    __try {
        if (h != INVALID_HANDLE_VALUE && name)
            NoteOpen(h, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return h;
}

BOOL WINAPI HookReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD read, LPOVERLAPPED ov) {
    // Only time the call when the handle is one we track, so untracked I/O pays
    // nothing but the lookup.
    int idx = -1;
    if (g_csReady) {
        EnterCriticalSection(&g_cs);
        idx = LookupHandle(h);
        LeaveCriticalSection(&g_cs);
    }
    if (idx < 0)
        return OrigReadFile(h, buf, toRead, read, ov);

    uint64_t t0 = Now();
    BOOL ok = OrigReadFile(h, buf, toRead, read, ov);
    uint64_t dt = Now() - t0;

    __try {
        DWORD got = (read && ok) ? *read : 0;
        EnterCriticalSection(&g_cs);
        FileStat& f = g_files[idx];
        f.reads++;
        f.bytes += got;
        f.readTicks += dt;
        f.sizeHist[BucketOf(toRead)]++;
        f.lastTick = Now();
        if (f.nextOffset >= 0)
            f.nextOffset += got;
        LeaveCriticalSection(&g_cs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ok;
}

void NoteSeek(HANDLE h, int64_t target) {
    if (!g_csReady)
        return;
    EnterCriticalSection(&g_cs);
    int idx = LookupHandle(h);
    if (idx >= 0) {
        FileStat& f = g_files[idx];
        f.seeks++;
        if (f.nextOffset >= 0) {
            int64_t delta = target - f.nextOffset;
            if (delta < 0) {
                f.backSeeks++;
                delta = -delta;
            }
            f.seekBytes += (uint64_t)delta;
        }
        f.nextOffset = target;
    }
    LeaveCriticalSection(&g_cs);
}

DWORD WINAPI HookSetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method) {
    DWORD r = OrigSetFilePointer(h, lo, hi, method);
    __try {
        if (r != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR) {
            int64_t pos = (int64_t)r;
            if (hi)
                pos |= ((int64_t)*hi) << 32;
            NoteSeek(h, pos);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

BOOL WINAPI HookSetFilePointerEx(HANDLE h, LARGE_INTEGER dist, PLARGE_INTEGER newPos,
                                 DWORD method) {
    LARGE_INTEGER local;
    local.QuadPart = 0;
    BOOL ok = OrigSetFilePointerEx(h, dist, newPos ? newPos : &local, method);
    __try {
        if (ok)
            NoteSeek(h, newPos ? newPos->QuadPart : local.QuadPart);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ok;
}

// A mapping handle inherits the file's slot, so a later MapViewOfFile can be
// charged to the file it actually reads.
void NoteMapping(HANDLE file, HANDLE mapping) {
    if (!g_csReady || !mapping)
        return;
    EnterCriticalSection(&g_cs);
    int idx = LookupHandle(file);
    if (idx >= 0)
        BindHandle(mapping, idx);
    LeaveCriticalSection(&g_cs);
}

HANDLE WINAPI HookCreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot,
                                     DWORD hi, DWORD lo, LPCSTR name) {
    HANDLE m = OrigCreateFileMappingA(file, sa, prot, hi, lo, name);
    __try {
        NoteMapping(file, m);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return m;
}

HANDLE WINAPI HookCreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot,
                                     DWORD hi, DWORD lo, LPCWSTR name) {
    HANDLE m = OrigCreateFileMappingW(file, sa, prot, hi, lo, name);
    __try {
        NoteMapping(file, m);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return m;
}

LPVOID WINAPI HookMapViewOfFile(HANDLE mapping, DWORD access, DWORD offHi, DWORD offLo,
                                SIZE_T bytes) {
    LPVOID p = OrigMapViewOfFile(mapping, access, offHi, offLo, bytes);
    __try {
        if (p && g_csReady) {
            EnterCriticalSection(&g_cs);
            int idx = LookupHandle(mapping);
            if (idx >= 0) {
                g_files[idx].maps++;
                g_files[idx].mapBytes += bytes;
                g_files[idx].lastTick = Now();
            }
            LeaveCriticalSection(&g_cs);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return p;
}

// --- reporting -------------------------------------------------------------

void BuildReportPath() {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        strcpy_s(g_reportPath, "swloadprof.log");
        return;
    }
    for (DWORD i = n; i > 0; i--) {
        if (exe[i - 1] == '\\') {
            exe[i] = 0;
            break;
        }
    }
    sprintf_s(g_reportPath, "%sswloadprof.log", exe);
}

} // namespace

void LoadProfDumpReport() {
    if (!g_csReady)
        return;

    FILE* fp = nullptr;
    if (fopen_s(&fp, g_reportPath, "w") != 0 || !fp)
        return;

    EnterCriticalSection(&g_cs);

    uint64_t totalBytes = 0, totalReads = 0, totalTicks = 0, totalSeeks = 0, totalBack = 0;
    uint64_t totalMaps = 0, totalMapBytes = 0;
    for (size_t i = 0; i < g_fileCount; i++) {
        totalBytes += g_files[i].bytes;
        totalReads += g_files[i].reads;
        totalTicks += g_files[i].readTicks;
        totalSeeks += g_files[i].seeks;
        totalBack += g_files[i].backSeeks;
        totalMaps += g_files[i].maps;
        totalMapBytes += g_files[i].mapBytes;
    }

    fprintf(fp, "SoulMeter load profile\n");
    fprintf(fp, "uptime %.1fs  files %zu\n", MsSince(Now() - g_startTick) / 1000.0, g_fileCount);
    fprintf(fp, "total: %llu reads, %.1f MB, %.0f ms in ReadFile, %llu seeks (%llu backward)\n",
            (unsigned long long)totalReads, (double)totalBytes / (1024.0 * 1024.0),
            MsSince(totalTicks), (unsigned long long)totalSeeks,
            (unsigned long long)totalBack);
    if (totalReads)
        fprintf(fp, "avg read %.0f bytes, avg %.3f ms/read\n",
                (double)totalBytes / (double)totalReads, MsSince(totalTicks) / (double)totalReads);
    // Mapped bytes never reach ReadFile, so a file with mappings and few reads
    // is being paged in, not streamed, and its cost is invisible above.
    fprintf(fp, "mapped: %llu views, %.1f MB\n", (unsigned long long)totalMaps,
            (double)totalMapBytes / (1024.0 * 1024.0));

    fprintf(fp, "\n%-34s %8s %10s %9s %8s %7s  %s\n", "file", "reads", "MB", "ms", "seeks",
            "back", "read sizes <4K/16K/64K/256K/1M/+");
    for (size_t i = 0; i < g_fileCount; i++) {
        const FileStat& f = g_files[i];
        if (!f.reads && !f.opens)
            continue;
        fprintf(fp, "%-34s %8llu %10.2f %9.1f %8llu %7llu  %llu/%llu/%llu/%llu/%llu/%llu",
                f.path, (unsigned long long)f.reads, (double)f.bytes / (1024.0 * 1024.0),
                MsSince(f.readTicks), (unsigned long long)f.seeks,
                (unsigned long long)f.backSeeks,
                (unsigned long long)f.sizeHist[0], (unsigned long long)f.sizeHist[1],
                (unsigned long long)f.sizeHist[2], (unsigned long long)f.sizeHist[3],
                (unsigned long long)f.sizeHist[4], (unsigned long long)f.sizeHist[5]);
        if (f.maps)
            fprintf(fp, "  mapped %llu/%.1fMB", (unsigned long long)f.maps,
                    (double)f.mapBytes / (1024.0 * 1024.0));
        fprintf(fp, "\n");
    }

    fprintf(fp, "\nfirst/last access, seconds since inject\n");
    for (size_t i = 0; i < g_fileCount; i++) {
        const FileStat& f = g_files[i];
        if (!f.reads && !f.maps)
            continue;
        fprintf(fp, "%-34s %8.2f %8.2f  opens %llu\n", f.path,
                MsSince(f.firstTick - g_startTick) / 1000.0,
                MsSince(f.lastTick - g_startTick) / 1000.0, (unsigned long long)f.opens);
    }

    LeaveCriticalSection(&g_cs);
    fclose(fp);
}

bool LoadProfInstall() {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return true;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpf = (uint64_t)f.QuadPart;
    g_startTick = Now();

    InitializeCriticalSection(&g_cs);
    g_csReady = true;
    BuildReportPath();

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    struct { const char* name; void* detour; void** orig; } targets[] = {
        { "CreateFileW",      (void*)&HookCreateFileW,      (void**)&OrigCreateFileW },
        { "CreateFileA",      (void*)&HookCreateFileA,      (void**)&OrigCreateFileA },
        { "ReadFile",         (void*)&HookReadFile,         (void**)&OrigReadFile },
        { "SetFilePointer",   (void*)&HookSetFilePointer,   (void**)&OrigSetFilePointer },
        { "SetFilePointerEx", (void*)&HookSetFilePointerEx, (void**)&OrigSetFilePointerEx },
        { "CreateFileMappingA", (void*)&HookCreateFileMappingA, (void**)&OrigCreateFileMappingA },
        { "CreateFileMappingW", (void*)&HookCreateFileMappingW, (void**)&OrigCreateFileMappingW },
        { "MapViewOfFile",    (void*)&HookMapViewOfFile,    (void**)&OrigMapViewOfFile },
    };

    for (auto& t : targets) {
        void* fn = (void*)GetProcAddress(k32, t.name);
        if (!fn)
            continue;
        if (MH_CreateHook(fn, t.detour, t.orig) == MH_OK)
            MH_EnableHook(fn);
    }

    return OrigReadFile != nullptr;
}

void LoadProfShutdown() {
    if (InterlockedCompareExchange(&g_installed, 0, 1) != 1)
        return;
    LoadProfDumpReport();
}
