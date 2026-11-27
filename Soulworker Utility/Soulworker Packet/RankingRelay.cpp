#include "pch.h"
#include ".\Soulworker Packet\RankingRelay.h"
#include "SWConfig.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr size_t kMaxQueueJobs = 32;
constexpr size_t kMaxChunksPerBoard = 16;
constexpr uint16_t kMagicGlobal = 51; // EN / TC
constexpr uint16_t kMagicKR = 12;
constexpr uint16_t kMagicJP = 7;

struct RelayJob {
    std::string region;
    uint8_t cmd = 0;
    uint8_t classFilter = 0;
    uint32_t seasonId = 0;
    uint64_t updatedAt = 0;
    std::vector<std::vector<uint8_t>> chunks;
};

struct ChunkBuffer {
    uint32_t seasonId = 0;
    uint64_t updatedAt = 0;
    std::vector<std::vector<uint8_t>> chunks;
};

struct DedupKey {
    std::string region;
    uint8_t cmd = 0;
    uint8_t classFilter = 0;
    bool operator<(const DedupKey& o) const {
        return std::tie(region, cmd, classFilter) < std::tie(o.region, o.cmd, o.classFilter);
    }
};

struct DedupVal {
    uint32_t seasonId = 0;
    uint64_t updatedAt = 0;
    uint64_t hash = 0;
};

struct ChunkKey {
    uint8_t cmd = 0;
    uint8_t classFilter = 0;
    bool operator<(const ChunkKey& o) const {
        return std::tie(cmd, classFilter) < std::tie(o.cmd, o.classFilter);
    }
};

std::mutex g_mu;
HANDLE g_wakeEvent = nullptr;
HANDLE g_workerThread = nullptr;
volatile LONG g_running = 0;

bool g_enabled = false;
int g_regionOverride = 0; // 0=Auto, 1=EN, 2=TC, 3=KR, 4=JP
std::string g_detectedRegion;
std::string g_lastStatus = "Idle";
uint32_t g_sentCount = 0;

std::map<ChunkKey, ChunkBuffer> g_chunkBuffers;
std::map<DedupKey, DedupVal> g_dedupCache;
std::deque<RelayJob> g_jobQueue;

// Little-endian readers with strict bounds checks.
struct Reader {
    const uint8_t* data;
    size_t len;
    size_t pos = 0;

    bool ReadU8(uint8_t& out) {
        if (pos + 1 > len) return false;
        out = data[pos++];
        return true;
    }
    bool ReadU16(uint16_t& out) {
        if (pos + 2 > len) return false;
        out = (uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8);
        pos += 2;
        return true;
    }
    bool ReadU32(uint32_t& out) {
        if (pos + 4 > len) return false;
        out = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) |
              ((uint32_t)data[pos + 2] << 16) | ((uint32_t)data[pos + 3] << 24);
        pos += 4;
        return true;
    }
    bool ReadU64(uint64_t& out) {
        if (pos + 8 > len) return false;
        out = 0;
        for (int i = 0; i < 8; i++)
            out |= ((uint64_t)data[pos + i]) << (8 * i);
        pos += 8;
        return true;
    }
    bool Skip(size_t n) {
        if (pos + n > len) return false;
        pos += n;
        return true;
    }
    bool SkipString() {
        uint16_t byteLen = 0;
        if (!ReadU16(byteLen)) return false;
        return Skip(byteLen);
    }
};

uint64_t Fnv1a64(uint64_t h, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = ((uint32_t)data[i]) << 16;
        if (i + 1 < len) v |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < len) v |= (uint32_t)data[i + 2];

        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? kTable[(v >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? kTable[v & 0x3F] : '=');
    }
    return out;
}

// Validate a raw S>C (68,2)/(68,7) MonthlyList payload and scrub `my_entry`
// so the uploading player's name, league, and personal record never leave the PC.
bool ParseAndScrubMonthlyChunk(
    const uint8_t* payload,
    size_t len,
    uint32_t& outSeasonId,
    uint8_t& outClassFilter,
    uint64_t& outUpdatedAt,
    bool& outLastChunk,
    std::vector<uint8_t>& outScrubbed)
{
    Reader r{ payload, len, 0 };
    uint8_t unk5 = 0;
    if (!r.ReadU32(outSeasonId) || !r.ReadU8(outClassFilter) ||
        !r.ReadU8(unk5) || !r.ReadU64(outUpdatedAt))
        return false;
    if (outClassFilter > 10)
        return false;

    const size_t headerEnd = r.pos; // 14 bytes

    // my_entry: 30 scalar bytes + wstring name + wstring league + u32 u40
    if (!r.Skip(30) || !r.SkipString() || !r.SkipString() || !r.Skip(4))
        return false;

    const size_t entriesStart = r.pos;

    uint16_t count = 0;
    if (!r.ReadU16(count) || count > 100)
        return false;

    for (uint16_t i = 0; i < count; i++) {
        // rank_all(4) rank_class(4) unk8(4) char_id(4) class_id(1) level(1) = 18
        if (!r.Skip(18) || !r.SkipString() || !r.Skip(4) || !r.SkipString() || !r.Skip(12))
            return false;
    }

    uint8_t lastFlag = 0;
    uint32_t unkTail = 0;
    if (!r.ReadU8(lastFlag) || !r.ReadU32(unkTail))
        return false;

    // Require exact wire match with zero trailing bytes.
    if (r.pos != len)
        return false;

    outLastChunk = (lastFlag != 0);

    // Rebuild payload with zeroed my_entry (30 scalars + u16(0) + u16(0) + u32(0) = 38 bytes).
    outScrubbed.clear();
    outScrubbed.reserve(headerEnd + 38 + (len - entriesStart));
    outScrubbed.insert(outScrubbed.end(), payload, payload + headerEnd);
    outScrubbed.insert(outScrubbed.end(), 38, 0);
    outScrubbed.insert(outScrubbed.end(), payload + entriesStart, payload + len);
    return true;
}

// Fallback for EN vs TC when SoulMeter was launched after login (missed (2,17)).
// Checks active TCP peer IPs of SoulWorker.exe against known EN/TC prefixes.
std::string DetectGlobalRegionFromTcpTable() {
    DWORD gamePid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"SoulWorker.exe") == 0) {
                    gamePid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (!gamePid)
        return "";

    DWORD sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (sz == 0)
        return "";

    std::vector<uint8_t> buf(sz);
    if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != NO_ERROR)
        return "";

    const MIB_TCPTABLE_OWNER_PID* table = (const MIB_TCPTABLE_OWNER_PID*)buf.data();
    for (DWORD i = 0; i < table->dwNumEntries; i++) {
        const MIB_TCPROW_OWNER_PID& row = table->table[i];
        if (row.dwOwningPid != gamePid || row.dwState != MIB_TCP_STATE_ESTAB)
            continue;
        uint32_t ip = row.dwRemoteAddr; // network byte order
        uint8_t a = (uint8_t)(ip & 0xFF);
        uint8_t b = (uint8_t)((ip >> 8) & 0xFF);
        if (a == 43 && b == 130)
            return "EN";
        if (a == 175 && b == 99)
            return "TC";
    }
    return "";
}

std::string ResolveRegionLocked(uint16_t magic) {
    switch (g_regionOverride) {
    case 1: return "EN";
    case 2: return "TC";
    case 3: return "KR";
    case 4: return "JP";
    default: break;
    }

    if (magic == kMagicKR) {
        g_detectedRegion = "KR";
        return "KR";
    }
    if (magic == kMagicJP) {
        g_detectedRegion = "JP";
        return "JP";
    }
    if (magic == kMagicGlobal) {
        if (g_detectedRegion == "EN" || g_detectedRegion == "TC")
            return g_detectedRegion;
        std::string probed = DetectGlobalRegionFromTcpTable();
        if (!probed.empty()) {
            g_detectedRegion = probed;
            return probed;
        }
    }
    return "";
}

bool PostRelayJob(const RelayJob& job, std::string& outStatus) {
    // Parse RANKING_RELAY_URL via WinHttpCrackUrl.
    std::wstring wurl;
    {
        const char* url = RANKING_RELAY_URL;
        int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
        if (n <= 1) {
            outStatus = "Bad relay URL";
            return false;
        }
        wurl.resize((size_t)n - 1);
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl.data(), n);
    }

    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t hostBuf[256] = { 0 };
    wchar_t pathBuf[512] = { 0 };
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = _countof(hostBuf);
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = _countof(pathBuf);
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
        outStatus = "URL parse failed";
        return false;
    }

    json body;
    body["region"] = job.region;
    body["cmd"] = (int)job.cmd;
    body["chunks"] = json::array();
    for (const auto& c : job.chunks)
        body["chunks"].push_back(Base64Encode(c.data(), c.size()));
    std::string payload = body.dump();

    std::wstring userAgent = L"SoulMeter/";
    {
        const char* ver = APP_VERSION;
        int vn = MultiByteToWideChar(CP_UTF8, 0, ver, -1, nullptr, 0);
        if (vn > 1) {
            std::wstring wver((size_t)vn - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, ver, -1, wver.data(), vn);
            userAgent += wver;
        }
    }

    HINTERNET hSession = WinHttpOpen(
        userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        outStatus = "WinHttpOpen failed";
        return false;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, uc.nPort, 0);
    if (hConnect) {
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hReq = WinHttpOpenRequest(
            hConnect, L"POST", pathBuf, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (hReq) {
            std::wstring headers = L"Content-Type: application/json\r\n";

            if (WinHttpSendRequest(
                    hReq, headers.c_str(), (DWORD)headers.size(),
                    (LPVOID)payload.data(), (DWORD)payload.size(),
                    (DWORD)payload.size(), 0) &&
                WinHttpReceiveResponse(hReq, nullptr))
            {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                WinHttpQueryHeaders(
                    hReq,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &statusCode,
                    &statusSize,
                    WINHTTP_NO_HEADER_INDEX);

                ok = (statusCode == 200 || statusCode == 202);
                char msg[96] = { 0 };
                sprintf_s(msg, "%s cls=%u -> HTTP %lu",
                          job.region.c_str(), (unsigned)job.classFilter, statusCode);
                outStatus = msg;
                LogInstance.WriteLog("[RankingRelay] %s (cmd=%u, chunks=%zu)",
                                     msg, (unsigned)job.cmd, job.chunks.size());
            } else {
                char msg[96] = { 0 };
                sprintf_s(msg, "HTTP send failed (%lu)", GetLastError());
                outStatus = msg;
                LogInstance.WriteLog("[RankingRelay] %s", msg);
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    } else {
        outStatus = "Connect failed";
    }

    WinHttpCloseHandle(hSession);
    return ok;
}

DWORD WINAPI RelayWorkerThread(LPVOID) {
    while (InterlockedCompareExchange(&g_running, 0, 0) != 0) {
        WaitForSingleObject(g_wakeEvent, 500);

        for (;;) {
            RelayJob job;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (g_jobQueue.empty())
                    break;
                job = std::move(g_jobQueue.front());
                g_jobQueue.pop_front();
            }

            std::string status;
            bool ok = PostRelayJob(job, status);
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (ok)
                    g_sentCount++;
                if (!status.empty())
                    g_lastStatus = status;
            }
        }
    }
    return 0;
}

} // namespace

void RankingRelayStart() {
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0)
        return;

    g_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_workerThread = CreateThread(nullptr, 0, RelayWorkerThread, nullptr, 0, nullptr);
}

void RankingRelayStop() {
    if (InterlockedExchange(&g_running, 0) == 0)
        return;
    if (g_wakeEvent)
        SetEvent(g_wakeEvent);
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 3000);
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }
    if (g_wakeEvent) {
        CloseHandle(g_wakeEvent);
        g_wakeEvent = nullptr;
    }
}

void RankingRelayConfigure(bool enabled, int regionOverride) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_enabled = enabled;
    g_regionOverride = regionOverride;
}

void RankingRelayObserveServerSelectSend(uint16_t magic, const uint8_t* payload, size_t len) {
    if (magic != kMagicGlobal || !payload || len < 2)
        return;
    uint16_t serverId = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    std::lock_guard<std::mutex> lk(g_mu);
    if (serverId == 1)
        g_detectedRegion = "TC";
    else if (serverId == 2)
        g_detectedRegion = "EN";
}

void RankingRelayObserveServerSelectRecv(uint16_t magic, const uint8_t* payload, size_t len) {
    if (magic != kMagicGlobal || !payload || len < 4)
        return;
    uint16_t ipLen = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    if (ipLen == 0 || (size_t)2 + ipLen > len)
        return;
    std::string ip((const char*)(payload + 2), ipLen);
    std::lock_guard<std::mutex> lk(g_mu);
    if (ip.rfind("43.130.", 0) == 0)
        g_detectedRegion = "EN";
    else if (ip.rfind("175.99.", 0) == 0)
        g_detectedRegion = "TC";
}

void RankingRelayOnMonthlyPacket(uint16_t magic, uint8_t cmd, const uint8_t* payload, size_t len) {
    if (!payload || len < 52 || (cmd != 2 && cmd != 7))
        return;

    uint32_t seasonId = 0;
    uint8_t classFilter = 0;
    uint64_t updatedAt = 0;
    bool lastChunk = false;
    std::vector<uint8_t> scrubbed;

    if (!ParseAndScrubMonthlyChunk(payload, len, seasonId, classFilter, updatedAt, lastChunk, scrubbed))
        return;

    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_enabled)
        return;

    std::string region = ResolveRegionLocked(magic);
    if (region.empty()) {
        g_lastStatus = "Region unknown (select EN/TC in Options)";
        return;
    }

    ChunkKey ck{ cmd, classFilter };
    ChunkBuffer& buf = g_chunkBuffers[ck];
    if (buf.seasonId != seasonId || buf.updatedAt != updatedAt || buf.chunks.size() >= kMaxChunksPerBoard) {
        buf.seasonId = seasonId;
        buf.updatedAt = updatedAt;
        buf.chunks.clear();
    }
    buf.chunks.push_back(std::move(scrubbed));

    if (!lastChunk)
        return;

    // Deduplicate identical hourly snapshots locally.
    uint64_t h = 14695981039346656037ULL;
    for (const auto& c : buf.chunks)
        h = Fnv1a64(h, c.data(), c.size());

    DedupKey dk{ region, cmd, classFilter };
    auto it = g_dedupCache.find(dk);
    if (it != g_dedupCache.end() &&
        it->second.seasonId == seasonId &&
        it->second.updatedAt == updatedAt &&
        it->second.hash == h)
    {
        buf.chunks.clear();
        return;
    }
    g_dedupCache[dk] = DedupVal{ seasonId, updatedAt, h };

    if (g_jobQueue.size() < kMaxQueueJobs) {
        RelayJob job;
        job.region = region;
        job.cmd = cmd;
        job.classFilter = classFilter;
        job.seasonId = seasonId;
        job.updatedAt = updatedAt;
        job.chunks = std::move(buf.chunks);
        g_jobQueue.push_back(std::move(job));
        if (g_wakeEvent)
            SetEvent(g_wakeEvent);
    }
    buf.chunks.clear();
}

uint32_t RankingRelayGetSentCount() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_sentCount;
}

std::string RankingRelayGetDetectedRegion() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_detectedRegion;
}

std::string RankingRelayGetLastStatus() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_lastStatus;
}
