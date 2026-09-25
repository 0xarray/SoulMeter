#pragma once

#pragma pack(push, 1)

typedef struct _SWHEADER { // 9
	uint16_t _magic;
	uint16_t _size;
	unsigned char _const_value01;
	unsigned char key;
	uint16_t _op;
	unsigned char unk01;
} SWHEADER;

#pragma pack(pop)

class SWPacket : public MemoryPool<SWPacket, 10> {
protected:
	SWHEADER* _swheader;
	uint8_t* _data;

	SWPacket() {}
	SWPacket(SWPacket& other) {}

public:
	SWPacket(SWHEADER* swheader, uint8_t* data) : _swheader(swheader), _data(data) {}
	virtual ~SWPacket() {}

	virtual void Debug() {}
	virtual void Do() {}
};