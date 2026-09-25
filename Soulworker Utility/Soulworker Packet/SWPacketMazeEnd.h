#pragma once
#include ".\Soulworker Packet\SWPacket.h"

#pragma pack(push, 1)

typedef struct _SWPACKETMAZEEND {
	BYTE _rank; // 0 : F, 1: C, 2: B, 3: A, 4 : S, 5: SS
	uint32_t _playScore;
	uint32_t _clearScore;
	uint32_t _clearTime; // 1587 = 158.7s


}SWPACKETCHATMAZEEND;
#pragma pack(pop)

class SWPacketMazeEnd : public SWPacket {
protected:
	SWPacketMazeEnd() {}

public:
	SWPacketMazeEnd(SWHEADER* swheader, BYTE* data);
	~SWPacketMazeEnd() {}

	void Do();
};