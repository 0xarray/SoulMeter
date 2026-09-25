#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)

typedef struct _SWPacketMazeUpdateStatePacket {
	
	uint32_t _stateID;
	BYTE _state;

}SWPacketMazeUpdateStatePacket;
#pragma pack(pop)

class SWPacketMazeUpdateState : public SWPacket {
protected:
	SWPacketMazeUpdateState() {}

public:
	SWPacketMazeUpdateState(SWHEADER* swheader, BYTE* data);
	~SWPacketMazeUpdateState() {}

	void Do();
};