#pragma once

#include ".\Soulworker Packet\PacketType.h"
#include ".\Soulworker Packet\SWPacket.h"

#define SWPACKETMAKER SWPacketMaker::getInstance()

#ifdef _DEBUG
#define DEBUG_RECV_CREATEPACKET 0
#ifndef DEBUG_RECV_DISPLAYPKT
#define DEBUG_RECV_DISPLAYPKT 0
#endif
#endif

class SWPacketMaker : public Singleton<SWPacketMaker> {
public:
	bool Init();
	SWHEADER* GetSWHeader(std::vector<unsigned char>& packet);
	void CreateSWPacket(std::vector<unsigned char>& packet);
};