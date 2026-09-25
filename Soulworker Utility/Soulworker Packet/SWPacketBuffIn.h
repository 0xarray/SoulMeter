#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPACKETBUFFIN {
	uint32_t _playerID;
	unsigned short _buffID;
	float _duration;
	BYTE _stack;
	uint32_t _giverID;
	BYTE _unknown02;
}SWPACKETBUFFIN;
#pragma pack(pop)

class SWPacketBuffIn : public SWPacket {
protected:
	SWPacketBuffIn() {}

public:
	SWPacketBuffIn(SWHEADER* swheader, BYTE* data);
	~SWPacketBuffIn() {}

	void Do();
};