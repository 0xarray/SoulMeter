#include "pch.h"
#include ".\Soulworker Packet\SWPacketMazeEnd.h"
#include ".\Damage Meter\Damage Meter.h"

SWPacketMazeEnd::SWPacketMazeEnd(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {

}

void SWPacketMazeEnd::Do() 
{
	SWPACKETCHATMAZEEND* pMazeEnd = (SWPACKETCHATMAZEEND*)(_data + sizeof(SWHEADER));

	DAMAGEMETER.SetMazeState(TRUE);
	DAMAGEMETER.Suspend();
	DAMAGEMETER.SetRealClearTime(pMazeEnd->_clearTime);
}
