#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPacketMonsterStatUpdate {
	uint32_t _id;
	BYTE _statCounts;
}SWPacketMonsterStatUpdatePkt;

typedef struct _SWPacketMonsterStatData {
	float _unk01;
	unsigned short _statID;
	uint64_t _statVal;
}SWPacketMonsterStatData;
#pragma pack(pop)

class SWPacketMonsterStatUpdate : public SWPacket {

protected:
	SWPacketMonsterStatUpdate() {}

public:
	SWPacketMonsterStatUpdate(SWHEADER* swheader, BYTE* data);
	~SWPacketMonsterStatUpdate() {}

	void Do();
};