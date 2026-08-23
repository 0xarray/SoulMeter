#pragma once
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#pragma pack(push, 1)

typedef struct _SWPACKET_IN_INFO_MONSTER1 {

	uint32_t id;

	float unk01;
	float unk02;
	float unk03;

	float unk04;
	uint64_t unk05;
	uint64_t unk06;
	uint32_t unk07;
	uint32_t unk08;
	BYTE unk09;
	uint32_t realDB2;

	uint32_t owner_id;
	uint32_t unk11;
	uint32_t unk12;
	BYTE unk13;
	uint32_t unk13b;
	double unk14;
	double unk15;
	BYTE data1Count;

}SWPACKET_IN_INFO_MONSTER1;

typedef struct _SWPACKET_IN_INFO_MONSTER_DATA1 {

	BYTE type;
	float val;

}SWPACKET_IN_INFO_MONSTER_DATA1;

typedef struct _SWPACKET_IN_INFO_MONSTER2 {

	uint64_t unk01;
	uint64_t unk02;
	float unk03;
	float unk03b;
	BYTE data2Count;

}SWPACKET_IN_INFO_MONSTER2;

typedef struct _SWPACKET_IN_INFO_MONSTER_DATA2 {

	uint32_t unk01;
	float unk02;
	BYTE unk03;
	uint32_t unk04;
	BYTE unk05;

}SWPACKET_IN_INFO_MONSTER_DATA2;

#pragma pack(pop)

class SWPacketInInfoMonster : public SWPacket {
protected:
	SWPacketInInfoMonster() {}

public:
	SWPacketInInfoMonster(SWHEADER* swheader, BYTE* data);
	~SWPacketInInfoMonster() {}

	void Do();
	void Log();
	void Debug();
};