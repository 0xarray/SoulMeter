#include "pch.h"
#include ".\Soulworker Packet\PacketType.h"
#include ".\Soulworker Packet\SWPacketMaker.h"
#include ".\Soulworker Packet\PipeReceiver.h"
#include ".\Soulworker Packet\Injector.h"
#include ".\Soulworker Packet\HookCommand.h"

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

	// Outbound channel for the maze hotkeys.
	DWORD cmdErr = HookCommandStart();
	if (cmdErr != ERROR_SUCCESS)
		LogInstance.WriteLog("Hook command channel init failed: %lu", cmdErr);

	return ERROR_SUCCESS;
}

void SWPacketMaker::CreateSWPacket(std::vector<unsigned char>& packet) {

	SWHEADER* swheader = GetSWHeader(packet);
	if (swheader == nullptr)
		return;
	BYTE* data = packet.data();

	// Constructors only store the pointers, so build outside the lock and
	// take it only for opcodes that actually touch the meter.
	SWPacket* swpacket = nullptr;
	const uint16_t op = _byteswap_ushort(swheader->_op);
	switch (swheader->_const_value01) {
	case 1: // recv
		switch (op) {
		case RecvOPcode::STATCHANGE:			swpacket = new SWPacketStatChange(swheader, data); break;
		case RecvOPcode::DEAD:					swpacket = new SWPacketDead(swheader, data); break;
		case RecvOPcode::CHARACTER_UPDATE_SPECIAL_OPTION_LIST:
			swpacket = new SWPacketcharacterUpdateSpecialOptionList(swheader, data); break;
		case RecvOPcode::SELECT_CHARACTER:
		case RecvOPcode::WORLDCHANGE:			swpacket = new SWPacketWorldChange(swheader, data); break;
		case RecvOPcode::MAZESTART:				swpacket = new SWPacketMazeStart(swheader, data); break;
		case RecvOPcode::IN_INFO_MONSTER:		swpacket = new SWPacketInInfoMonster(swheader, data); break;
		case RecvOPcode::OTHER_INFOS_MONSTER:	swpacket = new SWPacketOtherInfosMonster(swheader, data); break;
		case RecvOPcode::ENTER_ANIMATION:		swpacket = new SWPacketEnterAnimation(swheader, data); break;
		case RecvOPcode::OTHER_USESKILL:		swpacket = new SWPacketOtherUseSkill(swheader, data); break;
		case RecvOPcode::DAMAGE:				swpacket = new SWPacketDamage(swheader, data); break;
		case RecvOPcode::BUFFIN:				swpacket = new SWPacketBuffIn(swheader, data); break;
		case RecvOPcode::BUFFOUT:				swpacket = new SWPacketBuffOut(swheader, data); break;
		case RecvOPcode::AKASIC:				swpacket = new SWPacketAkasic(swheader, data); break;
		case RecvOPcode::COOLDOWN:				swpacket = new SWPacketCooldown(swheader, data); break;
		case RecvOPcode::BROOCH:				swpacket = new SWPacketBrooches(swheader, data); break;
		case RecvOPcode::MAZEEND:				swpacket = new SWPacketMazeEnd(swheader, data); break;
		case RecvOPcode::MAZE_UPDATE_STATE:		swpacket = new SWPacketMazeUpdateState(swheader, data); break;
		case RecvOPcode::PARTY:					swpacket = new SWPacketParty(swheader, data); break;
		case RecvOPcode::MONSTER_STAT_UPDATE:	swpacket = new SWPacketMonsterStatUpdate(swheader, data); break;
		case RecvOPcode::MONSTER_KILLED:		swpacket = new SWPacketMonsterKilled(swheader, data); break;
		case RecvOPcode::AGGRO_CHANGED:			swpacket = new SWPacketAggroChanged(swheader, data); break;
		case RecvOPcode::BIG_PARTY:				swpacket = new SWPacketBigParty(swheader, data); break;
		case RecvOPcode::SQUAD:					swpacket = new SWPacketSquad(swheader, data); break;
		}
		break;
	case 2: // send
		switch ((SendOPCode)op) {
		case SendOPCode::DODGE_USE:	swpacket = new SWSPacketMyDodgeUsed(swheader, data); break;
		case SendOPCode::SKILL_USE:	swpacket = new SWSPacketMySkillUsed(swheader, data); break;
		}
		break;
	case 3: // hook IPC
		switch (op) {
		case 0x0101: swpacket = new SWCPing(swheader, data); break;
		case 0x0102: swpacket = new SWCPresence(swheader, data); break;
		}
		break;
	}

#if DEBUG_RECV_DISPLAYPKT == 1
	LogInstance.WriteLog("OP : %04x\tsize : %04x\thandled : %d", op, swheader->_size, swpacket != nullptr);
	for (int i = 0; i < swheader->_size; i++)
		LogInstance.WriteLogNoDate("%02x ", data[i]);
	LogInstance.WriteLogNoDate("\n\n");
#endif

	if (swpacket == nullptr)
		return;

	DAMAGEMETER.GetLock();
#if DEBUG_RECV_CREATEPACKET == 1
	swpacket->Debug();
#endif
	swpacket->Do();
	DAMAGEMETER.FreeLock();

	delete swpacket;
}
