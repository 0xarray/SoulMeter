#pragma once

#include ".\Damage Meter\Damage Meter.h"
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)

typedef struct _SWPACKET_MYSKILLUSED {
	uint32_t _skillId;
	uint32_t _playerId;
	float _locationX;
	float _locationY;
	float _locationZ;
	float _direction;
	BYTE _unknown01[30];
}SWPACKET_MYSKILLUSED;

#pragma pack(pop)


class SWSPacketMySkillUsed : public SWPacket {
protected:
	SWSPacketMySkillUsed() {}
	SWSPacketMySkillUsed(SWPacket& other) {}

public:
	SWSPacketMySkillUsed(SWHEADER* swheader, BYTE* data);
	virtual ~SWSPacketMySkillUsed() {}

	virtual VOID Do();
};