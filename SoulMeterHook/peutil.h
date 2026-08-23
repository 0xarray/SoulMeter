#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace pe {

struct Section {
    uint8_t* data;
    size_t size;
};

inline IMAGE_NT_HEADERS64* NtHeaders(uint8_t* base) {
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    return nt;
}

// The module is mapped, so spans come from VirtualSize rather than the raw size.
inline bool FindSection(uint8_t* base, const char* name, Section* out) {
    IMAGE_NT_HEADERS64* nt = NtHeaders(base);
    if (!nt)
        return false;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        char n[9] = { 0 };
        memcpy(n, sec[i].Name, 8);
        if (strcmp(n, name) != 0)
            continue;
        out->data = base + sec[i].VirtualAddress;
        out->size = sec[i].Misc.VirtualSize;
        return out->size != 0;
    }
    return false;
}

// Null unless the pattern occurs exactly once; ambiguity means the signature
// stopped identifying the function, so fail rather than hook the wrong one.
inline const uint8_t* FindUnique(const Section& s, const uint8_t* pat, const char* mask) {
    size_t len = strlen(mask);
    if (mask[0] != 'x' || s.size < len)
        return nullptr;

    const uint8_t* found = nullptr;
    const uint8_t* p = s.data;
    const uint8_t* limit = s.data + s.size - len;
    while (p <= limit) {
        p = (const uint8_t*)memchr(p, pat[0], (size_t)(limit - p) + 1);
        if (!p)
            break;
        bool hit = true;
        for (size_t k = 1; k < len; k++) {
            if (mask[k] == 'x' && p[k] != pat[k]) {
                hit = false;
                break;
            }
        }
        if (hit) {
            if (found)
                return nullptr;
            found = p;
        }
        p++;
    }
    return found;
}

inline bool Contains(const uint8_t* hay, size_t len, const uint8_t* needle, size_t needleLen) {
    if (len < needleLen)
        return false;
    for (size_t i = 0; i + needleLen <= len; i++) {
        if (memcmp(hay + i, needle, needleLen) == 0)
            return true;
    }
    return false;
}

} // namespace pe
