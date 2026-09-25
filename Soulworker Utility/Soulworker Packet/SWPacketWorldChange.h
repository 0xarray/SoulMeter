#pragma once
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#pragma pack(push, 1)

typedef struct _SWPACKETWORLDCHANGE {
	uint32_t _id;
	BYTE _unknwon1[20];
	unsigned short _worldID;
	BYTE _unknwon2[10];
	unsigned short _serverIPLen;
	uint8_t _serverIP;
}SWPACKETWORLDCHANGE;

#pragma pack(pop)

class SWPacketWorldChange : public SWPacket {
protected:
	SWPacketWorldChange() {}

public:
	SWPacketWorldChange(SWHEADER* swheader, BYTE* data);
	~SWPacketWorldChange() {}

	void Do();
};