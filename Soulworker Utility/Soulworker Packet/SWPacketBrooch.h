#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPACKETBROOCH {
	uint32_t _playerID;
	uint16_t _broochID;
}SWPACKETBROOCH;
#pragma pack(pop)

// tb_CreateOption row ids, one per proc brooch family
enum BroochID {
	BROOCHID_FEVER = 0x0431,	// Technical BSK: Fever
	BROOCHID_FURY = 0x0427,		// Defense BSK: Fury
	BROOCHID_BACKSTEP = 0x4fcb,	// Defense SIN: Backstep
	BROOCHID_TECHNIC = 0x4fd2	// Technical SIN: Technic
};

class SWPacketBrooches : public SWPacket {
protected:
	SWPacketBrooches() {}

public:
	SWPacketBrooches(SWHEADER* swheader, BYTE* data);
	~SWPacketBrooches() {}

	void Do();
	void Log();
	void Debug();
};