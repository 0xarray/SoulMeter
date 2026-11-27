#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

// Passive monthly ranking relay.
//
// Listens to already-decrypted S>C (68,2) / (68,7) ranking replies streamed
// from SoulMeterHook, strips the caller's personal `my_entry` block, buffers
// multi-chunk replies until `last_chunk == 1`, and POSTs the public board to
// sw-ranked on a dedicated background WinHTTP thread. Never sends any packets
// to the game server.

void RankingRelayStart();
void RankingRelayStop();

// Configuration pushed from UiOption.
// regionOverride: 0 = Auto, 1 = EN, 2 = TC, 3 = KR, 4 = JP.
void RankingRelayConfigure(bool enabled, int regionOverride);

// Passive observers called from SWPacketMaker (never block).
void RankingRelayObserveServerSelectSend(uint16_t magic, const uint8_t* payload, size_t len);
void RankingRelayObserveServerSelectRecv(uint16_t magic, const uint8_t* payload, size_t len);
void RankingRelayOnMonthlyPacket(uint16_t magic, uint8_t cmd, const uint8_t* payload, size_t len);

// Thread-safe status for the Options UI.
uint32_t RankingRelayGetSentCount();
std::string RankingRelayGetDetectedRegion();
std::string RankingRelayGetLastStatus();
