#pragma once

// The client is an IOCP application (XIOCPClientEx) and issues overlapped
// WSARecv, so ws2_32 detours never observe a byte. These hooks target the
// game's own (de)serialisers instead, taken from two adjacent netMgr vtable
// slots:
//   serialise  : (netMgr, mode, pkt, dst, u16* outLen)
//   deobfuscate: (netMgr, mode, seq, src, dst)
// Both receive the crypto mode as an argument, so one hook per direction covers
// plaintext and obfuscated traffic alike, and the game has already reassembled
// the stream by the time either is called. Neither slot index is assumed - GB
// and KR order them serialise, deobfuscate; JP the other way round - so both are
// found by signature and the same DLL binds against all three clients.

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
