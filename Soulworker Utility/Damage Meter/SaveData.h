#pragma once
#include "pch.h"
#include ".\Third Party\FlatBuffers\include\SW_HISTORY_.h"
#include ".\Damage Meter\History.h"


using namespace SoulMeterFBS::History;

#define SAVEDATA SWSaveData::getInstance()

#ifdef _DEBUG
#define DEBUG_SAVEDATA_SAVE 0
#define DEBUG_SAVEDATA_DELETE 0
#endif

// XOR( [Loop [[History Size]-[History Data]]  ] )
class SWSaveData : public Singleton<SWSaveData> {

private:
	const uint32_t _saveVersion = 1;
	const std::string _oriSaveFileName = "SoulMeter.dat";
	// How many meters can run side by side, each on its own history file.
	// Matches kMaxMeters in the hook, which is what caps how many of them the
	// capture pipe actually feeds.
	static const int _maxInstances = 8;
	std::string _saveFileName;
	std::fstream _saveFile;

	bool _fileNotExist = FALSE;
	bool _inited = FALSE;

	// 1 -> "SoulMeter.dat", 2 -> "SoulMeter_2.dat", ...
	std::string InstanceSaveFileName(int instance);

	bool Load();
	void Crypt(unsigned char* src, unsigned char* dest, LONG64 len);

	void ReadSaveData(LONG64& offset);

	void WriteData(unsigned char* buf, LONG64 size, std::fstream* pFS = nullptr);
	void ReadData(unsigned char* buf, LONG64 size, LONG64 offset);
	LONG64 GetNextSaveDataLength(LONG64& offset);

	std::mutex _mutex;

public:
	~SWSaveData();

	DWORD Init(std::string fileName = "");
	LONG64 GetCurrentLength();
	void Save(flatbuffers::FlatBufferBuilder& fbb);
	void Delete(LONG64 index, LONG64 clearCount = -1);
	void Clone(std::string filename);
	void Reset();

	void GetLock()
	{
		_mutex.lock();
	}
	void FreeLock()
	{
		_mutex.unlock();
	}

	const std::string CurrentSaveDataPath()
	{
		return _saveFileName;
	}
};