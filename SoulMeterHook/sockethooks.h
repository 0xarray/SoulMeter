#pragma once

// The client is an IOCP application (XIOCPClientEx) and issues overlapped
// WSARecv, so ws2_32 detours never observe a byte. These hooks target the
// game's own (de)serialisers instead, taken from the netMgr vtable:
//   vtbl[1] (+8)  serialise  : (netMgr, mode, pkt, dst, u16* outLen)
//   vtbl[2] (+16) deobfuscate: (netMgr, mode, seq, src, dst)
// Both receive the crypto mode as an argument, so one hook per direction covers
// plaintext and obfuscated traffic alike, and the game has already reassembled
// the stream by the time either is called. The table is located by signature
// rather than a pinned address, so the same DLL binds against both the Global
// and KR clients.

#include <windows.h>
#include <cstdint>

#include "stream.h"

extern ByteQueue g_frameQueue;

bool HookInstall();
void HookUninstall();
bool HooksAreLive();

extern volatile LONG g_pingMs;
extern volatile LONG64 g_lastPingAt;
void BuildPingFrame(uint8_t* out, size_t* outLen);
