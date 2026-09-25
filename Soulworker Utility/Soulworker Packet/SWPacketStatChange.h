#pragma once
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#pragma pack(push, 1)

typedef struct _SWPACKETSTATCHANGE_HEADER {
	BYTE _unknown01;
	uint32_t _playerID;
	BYTE _statsCount;
}SWPACKETSTATCHANGE_HEADER;

typedef struct _SWPACKETSTATCHANGE_DATA {
	float _statValue;
	unsigned short _statType;
	BYTE _unknown01[8]; //0430new
}SWPACKETSTATCHANGE_DATA;

#pragma pack(pop)

class SWPacketStatChange : public SWPacket {

protected:
	SWPacketStatChange() {}

public:
	SWPacketStatChange(SWHEADER* swheader, BYTE* data);
	~SWPacketStatChange() {}

	void Do();
	void Debug();
};