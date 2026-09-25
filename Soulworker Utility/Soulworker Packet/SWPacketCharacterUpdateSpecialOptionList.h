#pragma once
#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPacketcharacterUpdateSpecialOptionListHeader {

	uint32_t _playerID;
	BYTE _listCount;

}SWPacketcharacterUpdateSpecialOptionListHeader;

typedef struct _SWPacketcharacterUpdateSpecialOptionVal {

	unsigned short _listID;
	float _listVal;

}SWPacketcharacterUpdateSpecialOptionVal;
#pragma pack(pop)

class SWPacketcharacterUpdateSpecialOptionList : public SWPacket {
protected:
	SWPacketcharacterUpdateSpecialOptionList() {}

public:
	SWPacketcharacterUpdateSpecialOptionList(SWHEADER* swheader, BYTE* data);
	~SWPacketcharacterUpdateSpecialOptionList() {}

	void Do();
};