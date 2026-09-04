#include "pch.h"
#include ".\Damage Meter\SaveData.h"

SWSaveData::~SWSaveData()
{
	bool isLock = _mutex.try_lock();

	if (_saveFile.is_open())
		_saveFile.close();

	FreeLock();
}

void SWSaveData::Reset()
{
	_inited = false;
	_saveFile.close();
	_saveFile.clear();
}

std::string SWSaveData::InstanceSaveFileName(int instance)
{
	if (instance <= 1)
		return _oriSaveFileName;

	std::string suffix = "_" + std::to_string(instance);
	size_t dot = _oriSaveFileName.find_last_of('.');
	if (dot == std::string::npos)
		return _oriSaveFileName + suffix;
	return _oriSaveFileName.substr(0, dot) + suffix + _oriSaveFileName.substr(dot);
}

DWORD SWSaveData::Init(std::string fileName)
{
	DWORD error = ERROR_FILE_NOT_FOUND;

	// The history file is held deny read/write, so meters cannot share one.
	// A second instance therefore falls through to SoulMeter_2.dat, a third to
	// SoulMeter_3.dat, and so on; the first one started keeps the plain name.
	const bool useDefaultName = fileName.size() == 0;

	do
	{
		for (int instance = 1; instance <= _maxInstances; instance++)
		{
			if (useDefaultName)
				_saveFileName = InstanceSaveFileName(instance);
			else
				_saveFileName = fileName;
			// Open save data, set deny read/write
			while (TRUE)
			{
				_saveFile.open(_saveFileName, std::ios::in | std::ios::out | std::ios::binary, _SH_DENYRW);
				if (!_saveFile.is_open())
				{
					_saveFile.close();

					_saveFile.open(_saveFileName, std::ios::out, _SH_DENYRW);
					if (!_saveFile)
					{
						error = ERROR_FILE_SYSTEM_LIMITATION;
						break;
					}
					_saveFile.close();
				}
				else {
					error = ERROR_SUCCESS;
					break;
				}
			}

			// Anything but another meter holding this name is a real failure.
			if (error != ERROR_FILE_SYSTEM_LIMITATION || !useDefaultName)
				break;

			_saveFile.clear();
		}

		if (!_inited && error == ERROR_SUCCESS)
		{
			try {
				if (!Load()) {
					throw "Load failed";
				}
			}
			catch (...)
			{
				error = ERROR_FILE_CORRUPT;
				break;
			}
		}

	} while (false);

	return error;
}

bool SWSaveData::Load()
{
	LONG64 fileSize = GetCurrentLength();
	LONG64 processedHistory = 0;
	// file version
	if (fileSize >= sizeof(_saveVersion))
	{
		LONG64 offset = 0;
		uint32_t fileVersion = 0;

		ReadData((unsigned char*)&fileVersion, sizeof(_saveVersion), offset);
		offset += sizeof(_saveVersion);

		// Check version
		if (fileVersion != _saveVersion)
		{
			return false;
		}

		// Unserialization
		while (fileSize > offset)
		{
			ReadSaveData(offset);
			processedHistory++;
		}

	}
	else {
		_fileNotExist = TRUE;
	}

	_inited = TRUE;

	if (processedHistory > 0)
	{
		LONG64 clearCount = processedHistory - HISTORY_SIZE;
#if DEBUG_SAVEDATA_DELETE == 1
		LogInstance.WriteLog("[SWSaveData::Load] Loaded data = %llu", processedHistory);
#endif
		if (clearCount > 0)
			Delete(-1, clearCount);
	}

	return true;
}

void SWSaveData::ReadSaveData(LONG64& offset)
{
	LONG64 dataSize = GetNextSaveDataLength(offset);
	if (dataSize > 0)
	{
		unsigned char* pSaveData = new unsigned char[dataSize];

		ReadData(pSaveData, dataSize, offset);
		offset += dataSize;

		HISTORY.UnSerialization(Get_tHistory(pSaveData));

		delete[] pSaveData;
	}
}

void SWSaveData::Crypt(unsigned char* src, unsigned char* dest, LONG64 len)
{
	for (LONG64 i = 0; i < len; i++)
		dest[i] = src[i] ^ (12 + 14);
}

LONG64 SWSaveData::GetCurrentLength()
{
	LONG64 size = 0;

	_saveFile.seekg(0, std::ios::end);

	size = _saveFile.tellg();

	_saveFile.seekg(0, std::ios::beg);

	return size;
}

void SWSaveData::Save(flatbuffers::FlatBufferBuilder& fbb)
{
	bool isLock = _mutex.try_lock();
	{
		do
		{
			if (_saveFile.is_open())
			{
				_saveFile.close();

				std::string tmpFileName = _saveFileName + std::string(".tmp");

				// remove tmp file
				std::remove(tmpFileName.c_str());

				// copy current save file to tmp
				std::filesystem::copy(_saveFileName, tmpFileName);

				// open tmp file
				std::fstream tmpFile;
				tmpFile.open(tmpFileName, std::ios::in | std::ios::out | std::ios::binary, _SH_DENYRW);
				if (!tmpFile.is_open())
				{
					LogInstance.WriteLog("[SWSaveData::Save] open tmp file failed");
					exit(1);
				}

				// go to end
				tmpFile.clear();
				tmpFile.seekp(0, std::ios::end);

				// write save data version
				if (_fileNotExist)
				{
					_fileNotExist = FALSE;
					WriteData((unsigned char*)&_saveVersion, sizeof(uint32_t), &tmpFile);
				}

				// write savedata size
				LONG64 size = fbb.GetSize();
				WriteData((unsigned char*)&size, sizeof(LONG64), &tmpFile);

				// write savedata
				WriteData((unsigned char*)fbb.GetBufferPointer(), size, &tmpFile);

				// flush tmp file
				tmpFile.close();

				// delete old savedata and rename tmp file to savedata
				std::remove(_saveFileName.c_str());
				if (std::rename(tmpFileName.c_str(), _saveFileName.c_str()))
				{
					LogInstance.WriteLog("[SaveData::Save] rename failed");
					exit(1);
				}

				// reopen savedata
				Init(_saveFileName);

#if DEBUG_SAVEDATA_SAVE == 1
				LogInstance.WriteLog("[SWSaveData::Save] Data saved, size = %llu", size);
#endif
			}
		} while (false);

		if (isLock)
			FreeLock();
	}
}

void SWSaveData::Delete(LONG64 index, LONG64 clearCount)
{
	bool isLock = _mutex.try_lock();
	{
		
		do
		{
			if (!_inited || !_saveFile.is_open())
				break;

			LONG64 currentSize = GetCurrentLength();
			if (currentSize <= sizeof(_saveVersion))
				break;

			std::string tmpFileName = _saveFileName + std::string(".tmp");
			std::remove(tmpFileName.c_str());

			std::fstream tmpFile;
			tmpFile.open(tmpFileName, std::ios::out | std::ios::binary, _SH_DENYRW);
			if (!tmpFile.is_open())
			{
				LogInstance.WriteLog("[SWSaveData::Save] open tmp file failed");
				exit(1);
			}

			WriteData((unsigned char*)&_saveVersion, sizeof(uint32_t), &tmpFile);

			LONG64 offset = sizeof(_saveVersion);
			LONG64 i = 0;
			LONG64 prevSize = 0;
			unsigned char* tmpData = nullptr;
			while (currentSize > offset)
			{
				i++;

				LONG64 dataSize = GetNextSaveDataLength(offset);
				if (index != i && i > clearCount)
				{
					if (dataSize > prevSize)
					{
						delete[] tmpData;
						tmpData = new unsigned char[dataSize];
					}
					// read data from savedata
					ReadData(tmpData, dataSize, offset);

					// write datalength to tmp file
					WriteData((unsigned char*)&dataSize, sizeof(dataSize), &tmpFile);

					// write data to tmp file
					WriteData(tmpData, dataSize, &tmpFile);

					// flush tmp file
					tmpFile.flush();

					prevSize = dataSize;
				}
				offset += dataSize;
			}
#if DEBUG_SAVEDATA_DELETE == 1
			LogInstance.WriteLog("[SWSaveData::Delete] clearIndex = %llu, clearCount = %llu", index, clearCount);
#endif
			delete[] tmpData;

			_saveFile.close();
			tmpFile.close();

			// delete old savedata and rename tmp file to savedata
			std::remove(_saveFileName.c_str());
			if (std::rename(tmpFileName.c_str(), _saveFileName.c_str()))
			{
				LogInstance.WriteLog("[SaveData::Delete] rename failed");
				exit(1);
			}

			// reopen savedata
			Init(_saveFileName);
		} while (false);

		if (isLock)
			FreeLock();
	}
}

void SWSaveData::Clone(std::string filename)
{
	bool isLock = _mutex.try_lock();
	{
		if (_saveFile.is_open())
		{
			_saveFile.close();

			std::filesystem::copy(_saveFileName, filename);

			Init(_saveFileName);
		}
		if (isLock)
			_mutex.unlock();
	}
}

void SWSaveData::WriteData(unsigned char* buf, LONG64 size, std::fstream* pFS)
{
	if (pFS == nullptr)
		pFS = &_saveFile;

	unsigned char* tmp = new unsigned char[size];

	Crypt(buf, tmp, size);
	
	pFS->write((const char*)tmp, size);

	delete[] tmp;
}

void SWSaveData::ReadData(unsigned char* buf, LONG64 size, LONG64 offset)
{
	_saveFile.seekg(offset, std::ios::beg);

	_saveFile.read((char*)buf, size);

	Crypt(buf, buf, size);
}

LONG64 SWSaveData::GetNextSaveDataLength(LONG64& offset)
{
	LONG64 dataSize = 0;

	ReadData((unsigned char*)&dataSize, sizeof(LONG64), offset);
	offset += sizeof(LONG64);

	return dataSize;
}