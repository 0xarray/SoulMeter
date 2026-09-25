#pragma once
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"

#pragma pack(push, 1)

typedef struct _SWPACKETAKASIC {
	uint32_t _ownder_id;
	uint32_t _id;
}SWPACKETAKASIC;

#pragma pack(pop)

class SWPacketAkasic : public SWPacket {
protected:
	SWPacketAkasic() {}

public:
	SWPacketAkasic(SWHEADER* swheader, BYTE* data);
	~SWPacketAkasic() {}

	void Do();
};