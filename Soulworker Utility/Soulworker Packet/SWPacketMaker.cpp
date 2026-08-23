#include "pch.h"
#include ".\Soulworker Packet\PacketType.h"
#include ".\Soulworker Packet\SWPacketMaker.h"
#include ".\Soulworker Packet\SWCPacket.h"
#include ".\Soulworker Packet\PipeReceiver.h"
#include ".\Soulworker Packet\Injector.h"
#include ".\UI\PlayerTable.h"
#include "SWConfig.h"

SWHEADER* SWPacketMaker::GetSWHeader(std::vector<unsigned char>& packet) {

	if (packet.size() < sizeof(SWHEADER)) {
		return nullptr;
	}
	SWHEADER* swheader = (SWHEADER*)(&packet[0]);
	if (swheader->_const_value01 != 1 && swheader->_const_value01 != 2 && swheader->_const_value01 != 3)
	{
		return nullptr;
	}
	return swheader;

}

bool SWPacketMaker::Init() {
	// Packet capture is now DLL-injection based: SoulMeterHook.dll is injected
	// into the game process and streams captured frames over a named pipe.
	// The pipe server feeds every frame into CreateSWPacket below.
	DWORD pipeErr = PipeReceiverStart();
	if (pipeErr != ERROR_SUCCESS)
		LogInstance.WriteLog("Pipe receiver init failed: %lu", pipeErr);
	else
		LogInstance.WriteLog("Pipe receiver started");

	DWORD injErr = InjectorStart();
	if (injErr != ERROR_SUCCESS)
		LogInstance.WriteLog("Injector init failed: %lu", injErr);
	else
		LogInstance.WriteLog("Injector started");

	return ERROR_SUCCESS;
}

void SWPacketMaker::CreateSWPacket(std::vector<unsigned char>& packet) {

	SWHEADER* swheader = GetSWHeader(packet);
	BYTE* data = packet.data();

	if (swheader == nullptr || data == nullptr)
	{
		return;
	}

#if DEBUG_RECV_DISPLAY_ALL_PKT == 1
	LogInstance.WriteLog("OP : %04x\tsize : %04x", swheader->_op, swheader->_size);
	for (int i = 0; i < swheader->_size; i++)
		LogInstance.WriteLogNoDate(L"%02x ", data[i]);
	LogInstance.WriteLogNoDate(L"\n\n");
#endif
	SWPacket* swpacket = nullptr;
	char* opcode = "NO_OPCODE_FOUND";
	DAMAGEMETER.GetLock();
	{
		if (swheader->_const_value01 == 1) { //recv
			switch (_byteswap_ushort(swheader->_op)) {
				/* 0x01*/

				/*0x03*/
			case RecvOPcode::STATCHANGE: //0430
				opcode = "STATCHANGE";
				swpacket = new SWPacketStatChange(swheader, data);
				break;
			case RecvOPcode::DEAD:
				opcode = "DEAD";
				swpacket = new SWPacketDead(swheader, data);
				break;
			case RecvOPcode::CHARACTER_UPDATE_SPECIAL_OPTION_LIST:
				opcode = "character update special option list";
				swpacket = new SWPacketcharacterUpdateSpecialOptionList(swheader, data);
				break;

				/*0x04*/
				// eSUB_CMD_WORLD_ENTER_RES
			case RecvOPcode::WORLDCHANGE: //0430
				opcode = "WORLDCHANGE";
				swpacket = new SWPacketWorldChange(swheader, data);
				break;
			case RecvOPcode::MAZESTART:
				opcode = "MAZESTART";
				swpacket = new SWPacketMazeStart(swheader, data);
				break;
			case RecvOPcode::SPAWNED_CHARINFO:
				opcode = "SPAWNED_charINFO";
				//swpacket = new SWPacketSpawnedcharInfo(swheader, data);
				break;
			case RecvOPcode::IN_INFO_MONSTER: //0605
				opcode = "IN_INFO_MONSTER";
				swpacket = new SWPacketInInfoMonster(swheader, data);
				break;
			case RecvOPcode::OTHER_INFOS_MONSTER:
				opcode = "OTHER_INFOS_MONSTER";
				swpacket = new SWPacketOtherInfosMonster(swheader, data);
				break;

				/*0x05*/
			case RecvOPcode::STARTMOVE:
				opcode = "STARTMOVE";
				//swpacket = new SWPacketStartMove(swheader, data);
				break;
			case RecvOPcode::STOPMOVE:
				opcode = "STOPMOVE";
				break;
			case RecvOPcode::JUMP:
				opcode = "JUMP";
				break;
			case RecvOPcode::CANCEL_WITHMOVE:
				opcode = "CANCEL_WITHMOVE";
				break;
			case RecvOPcode::ENTER_ANIMATION:
				opcode = "ENTER_ANIMATION";
				swpacket = new SWPacketEnterAnimation(swheader, data);
				break;

				/*0x06 Combat*/
			//case RecvOPcode::EVADE:
			//	break;
			case RecvOPcode::USESKILL:
				opcode = "USESKIL:";
				//swpacket = new SWPacketUseSkill(swheader, data);
				break;
			case RecvOPcode::OTHER_USESKILL: //0430
				opcode = "OTHER_USESKILL";
				swpacket = new SWPacketOtherUseSkill(swheader, data);
				break;
			case RecvOPcode::DAMAGE: //0430
				opcode = "DAMAGE";
				swpacket = new SWPacketDamage(swheader, data);
				break;
			case RecvOPcode::BUFFIN: //0430
				opcode = "BUFFIN";
				swpacket = new SWPacketBuffIn(swheader, data);
				break;
			case RecvOPcode::BUFFOUT: //0430
				opcode = "BUFFOUT";
				swpacket = new SWPacketBuffOut(swheader, data);
				break;
			case RecvOPcode::PROJECTILE:
				opcode = "PROJECTILE";
				break;
			case RecvOPcode::AKASIC:
				opcode = "AKASIC";
				swpacket = new SWPacketAkasic(swheader, data);
				break;
			case RecvOPcode::COOLDOWN: //0430
				opcode = "COOLDOWN";
				swpacket = new SWPacketCooldown(swheader, data);
				break;

				/*0x07*/
			case RecvOPcode::CHAT:
				opcode = "CHAT";
				//swpacket = new SWPacketChat(swheader, data);
				break;

			case RecvOPcode::BROOCH: //0864
				swpacket = new SWPacketBrooches(swheader, data);
				break;

				/* 0x11 Maze*/
			case RecvOPcode::MAZEEND:
				opcode = "MAZEEND";
				swpacket = new SWPacketMazeEnd(swheader, data);
				break;
			case RecvOPcode::MAZE_UPDATE_STATE:
				opcode = "maze MAZE_UPDATE_STATE state";
				swpacket = new SWPacketMazeUpdateState(swheader, data);
				break;

				/* 0x12 Party */
			case RecvOPcode::PARTY: //1117
				opcode = "PARTY";
				swpacket = new SWPacketParty(swheader, data);
				break;
			case RecvOPcode::PARTY_LIST_INFO:
				opcode = "PARTY_LIST_INFO";
				//swpacket = new SWPacketPartyListInfo(swheader, data);
				break;

				/* 0x17 Monster */
			case RecvOPcode::MONSTER_STAT_UPDATE:
				opcode = "MONSTER_STAT_UPDATE";
				swpacket = new SWPacketMonsterStatUpdate(swheader, data);
				break;
			case RecvOPcode::MONSTER_KILLED: //0430
				opcode = "MONSTER_KILLED";
				swpacket = new SWPacketMonsterKilled(swheader, data);
				break;
			case RecvOPcode::AGGRO_CHANGED: //0430
				opcode = "AGGRO_CHANGED";
				swpacket = new SWPacketAggroChanged(swheader, data);
				break;

				/* 0x23 Gesture*/
			case RecvOPcode::GESTURE_USED:
				opcode = "GESTURE_USED";
				//swpacket = new SWPacketGestureUsed(swheader, data);
				break;

				/* 0x2e Force*/
				// 8 players party
			case RecvOPcode::BIG_PARTY: //1117
				opcode = "BIG_PARTY";
				swpacket = new SWPacketBigParty(swheader, data);
				break;


			case RecvOPcode::SQUAD:
				opcode = "SQUAD";
				swpacket = new SWPacketSquad(swheader, data);
				break;


			default:
				break;
			}

		}
		else if (swheader->_const_value01 == 2)
		{
			switch ((SendOPCode)_byteswap_ushort(swheader->_op)) {
				//case 0x0501:
				//	break;
				//case 0x0503:
				//	break;
				//case 0x0522:
				//	break;
			case SendOPCode::DODGE_USE:
				swpacket = new SWSPacketMyDodgeUsed(swheader, data);
				break;
			case SendOPCode::SKILL_USE:
				swpacket = new SWSPacketMySkillUsed(swheader, data);
				break;
			default:
				//swpacket = new SWSPacket(swheader, data);
				break;
			}
		}
		else if (swheader->_const_value01 == 3)
		{
			switch (_byteswap_ushort(swheader->_op))
			{
			case 0x0101:
				swpacket = new SWCPing(swheader, data);
				break;
			case 0x0102:
				swpacket = new SWCPresence(swheader, data);
				break;
			}
		}
		else
		{
			DAMAGEMETER.FreeLock();
			return;
	}

#if DEBUG_RECV_DISPLAYPKT == 1
		LogInstance.WriteLog("Found opcode %s\n", opcode);
		LogInstance.WriteLog("OP : %04x\tsize : %04x", _byteswap_unsigned short(swheader->_op), swheader->_size);
		for (int i = 0; i < swheader->_size; i++)
			LogInstance.WriteLogNoDate(L"%02x ", data[i]);
		LogInstance.WriteLogNoDate(L"\n\n");
#endif

		if (swpacket != nullptr) {
#if DEBUG_RECV_CREATEPACKET == 1
			swpacket->Debug();
#endif
			// Todo
			swpacket->Do();

			delete swpacket;
		}
	}
	DAMAGEMETER.FreeLock();

}