#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)
typedef struct _SWPACKETAGGROCHANGED {
	uint32_t _id;
	uint32_t _targetedId;
}SWPACKETAGGROCHANGED;
#pragma pack(pop)

class SWPacketAggroChanged : public SWPacket {

protected:
	SWPacketAggroChanged() {}

public:
	SWPacketAggroChanged(SWHEADER* swheader, BYTE* data);
	~SWPacketAggroChanged() {}

	void Do();
};