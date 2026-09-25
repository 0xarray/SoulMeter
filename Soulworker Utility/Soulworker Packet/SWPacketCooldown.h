#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPACKETCOOLDOWN {
	uint32_t _playerID;
	float _value;
}SWPACKETCOOLDOWN;
#pragma pack(pop)

class SWPacketCooldown : public SWPacket {
protected:
	SWPacketCooldown() {}

public:
	SWPacketCooldown(SWHEADER* swheader, BYTE* data);
	~SWPacketCooldown() {}

	void Do();
};