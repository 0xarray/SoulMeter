#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Soulworker Packet\SWPacketSquad.h"

SWPacketSquad::SWPacketSquad(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketSquad::Do() {

	size_t offset = sizeof(SWHEADER); //SWHEADER

	offset += 12; //unk01

	uint16_t msgLen = 0; //RECRUITMSG
	memcpy(&msgLen, _data + offset, 2);

	offset += 2; //msgLen

	offset += msgLen; //Recruitment message

	uint16_t squadLeaderNickSize = 0;
	memcpy(&squadLeaderNickSize, _data + offset, 2);

	offset += 2; //SQUADLEADERNICKSIZE

	offset += squadLeaderNickSize; //SQUADLEADERNICK

	offset += 12; //unk02

	uint8_t squadPlayerCount = 0;
	memcpy(&squadPlayerCount, _data + offset, 1);

	offset += 1; //SQUADPLAYERCOUNT

	offset += 3; //unk03

	for (int i = 0; i < squadPlayerCount; i++) {
		uint32_t playerId = 0;
		memcpy(&playerId, _data + offset, 4);

		offset += 4; //PLAYERID

		uint16_t playerNickSize = 0;
		memcpy(&playerNickSize, _data + offset, 2);

		offset += 2; //PLAYERNICKSIZE

		wchar_t utf16[MAX_NAME_LEN] = { 0 };
		memcpy_s(utf16, MAX_NAME_LEN * sizeof(wchar_t), _data + offset, playerNickSize);

		char utf8[MAX_NAME_LEN] = { 0 };
		if (!UTF16toUTF8(utf16, utf8, MAX_NAME_LEN)) {
			return;
		}

		offset += playerNickSize; //PLAYERNICK

		offset += 1; //D_unk01
		uint8_t playerJob = 0;
		memcpy(&playerJob, _data + offset, 1);

		DAMAGEMETER.InsertPlayerMetadata(playerId, utf8, playerJob);

		offset += 1; //PLAYERJOB

		offset += 62; //D_unk02
	}
}
