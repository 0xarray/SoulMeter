#pragma once
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#pragma pack(push, 1)

typedef struct _SWPACKETENTERANIMATION {
	uint32_t _player_id;
	uint32_t _type1;
	uint32_t _type2;
}SWPACKETENTERANIMATION;

#pragma pack(pop)

class SWPacketEnterAnimation : public SWPacket {
protected:
	SWPacketEnterAnimation() {}

public:
	SWPacketEnterAnimation(SWHEADER* swheader, BYTE* data);
	~SWPacketEnterAnimation() {}

	void Do();
};