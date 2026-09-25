#pragma once

#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#define SWPACKETBIGPARTY_DUMMY 46 //1117

#pragma pack(push, 1)

typedef struct _SWPACKETBIGPARTY_HEADER {
	BYTE _unknown01[4];
	uint32_t _posHostID;
	BYTE _unknown02[10];
	BYTE _posPlayerCount;
}SWPACKETBIGPARTY_HEADER;

typedef struct _SWPACKETBIGPARTY_DATA {
	uint32_t _playerID;
	unsigned short _nickSize;
}SWPACKETBIGPARTY_DATA;

#pragma pack(pop)

class SWPacketBigParty : public SWPacket {
protected:
	SWPacketBigParty() {}

public:
	SWPacketBigParty(SWHEADER* swheader, BYTE* data);
	~SWPacketBigParty() {}

	void Do();
};