#pragma once
#include ".\Damage Meter\Damage Meter.h"
#include ".\Damage Meter\MonsterList.h"
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)

typedef struct _SWPACKETDAMAGE_MONSTER {
	uint32_t _monsterID; // 4
	BYTE _unknown01; // 1
	BYTE _damageType; //1
	uint32_t _totalDMG; //4
	uint32_t _soulstoneDMG; // 4
	uint32_t _remainHP; //4
	float _unknownF1; // 4
	float _unknownF2; // 4
	float _unknownF3; // 4
	BYTE _unknown02[30]; // 0605
}SWPACKETDAMAGE_MONSTER;

typedef struct _SWPACKETDAMAGE_PLAYER {
	uint32_t _playerID;
	BYTE _unknown01[20]; //0403new
	uint32_t _skillID;
	BYTE _unknown02[2]; //0403new
	unsigned short _maxCombo;
	BYTE _unknown03[2]; //0403new
}SWPACKETDAMAGE_PLAYER;

#pragma pack(pop)

class SWPacketDamage : public SWPacket {
private:
	void AttackOfMonster();

protected:
	SWPacketDamage() {}

public:
	SWPacketDamage(SWHEADER* swheader, BYTE* data);
	~SWPacketDamage() {}

	void Do();
	void Debug();
	void Log() {};
};