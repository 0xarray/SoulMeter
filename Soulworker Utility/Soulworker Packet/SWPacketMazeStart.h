#pragma once
#include ".\Soulworker Packet\SWPacket.h"

class SWPacketMazeStart : public SWPacket {
protected:
	SWPacketMazeStart() {}

public:
	SWPacketMazeStart(SWHEADER* swheader, BYTE* data);
	~SWPacketMazeStart() {}

	void Do();
};