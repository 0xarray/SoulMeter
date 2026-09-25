#pragma once
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#pragma pack(push, 1)

typedef struct _SWPACKET_OTHERUSESKILL {
	uint32_t _playerId;
	uint32_t _skillId;
	float _locationX;
	float _locationY;
	float _locationZ;
	float _direction;
	//BYTE _unknown01[19]; // 0430new
}SWPACKET_OTHERUSESKILL;

#pragma pack(pop)

class SWPacketOtherUseSkill : public SWPacket {

private:
	char _skillName[SKILL_NAME_LEN];

protected:
	SWPacketOtherUseSkill() {}

public:
	SWPacketOtherUseSkill(SWHEADER* swheader, BYTE* data);
	~SWPacketOtherUseSkill() {}

	void Do();
};