#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPACKETBUFFOUT {
	uint32_t _playerID;
	unsigned short _buffID;
	BYTE _unknwon01;
	uint32_t _giverID;
}SWPACKETBUFFOUT;
#pragma pack(pop)

class SWPacketBuffOut : public SWPacket {
protected:
	SWPacketBuffOut() {}

public:
	SWPacketBuffOut(SWHEADER* swheader, BYTE* data);
	~SWPacketBuffOut() {}

	void Do();
};