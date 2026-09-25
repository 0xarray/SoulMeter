#pragma once
#include "Third Party\SQLite\include\sqlite3.h"

#define SWDB MySQL::getInstance()
#define SWDBPATH "SWDB.db"

class MySQL : public Singleton<MySQL> {
private:
	sqlite3* _db = nullptr;

	bool InitDB();
	void FreeDB();

	bool InitSkillDB();
	bool InitMonsterDB();
	bool InitMapDB();
	bool InitBuffDB();

	sqlite3_stmt* _skill_stmt = nullptr;
	sqlite3_stmt* _monster_stmt = nullptr;
	sqlite3_stmt* _map_stmt = nullptr;
	sqlite3_stmt* _buff_stmt = nullptr;

public:
	MySQL();
	~MySQL();

	bool Init();
	bool GetSkillName(uint32_t skillId, _Out_ char* out_buffer, _In_ size_t out_buffer_length);
	bool GetMonsterName(uint32_t DB2, _Out_ char* out_buffer, _In_ size_t out_buffer_length);
	bool GetMonsterType(uint32_t DB2, int32_t* type);
	bool GetMapName(uint32_t mapId, _Out_ char* out_buffer, _In_ size_t out_buffer_length);
	bool GetBuffName(uint32_t buffId, _Out_ char* out_buffer, _In_ size_t out_buffer_length, _Out_ char* out_buffer_desc = NULL, _In_ size_t out_buffer_desc_length = NULL);
};