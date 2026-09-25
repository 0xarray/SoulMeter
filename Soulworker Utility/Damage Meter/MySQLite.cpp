#include "pch.h"

#include ".\Damage Meter\MySQLite.h"

MySQL::MySQL() {
}

MySQL::~MySQL() {
	FreeDB();
}

bool MySQL::InitDB() {

	if (sqlite3_open(SWDBPATH, &_db) != SQLITE_OK) {
		LogInstance.WriteLog("Error in InitDB : %s", sqlite3_errmsg(_db));
		sqlite3_close(_db);
		_db = nullptr;

		return FALSE;
	}

	return TRUE;
}

void MySQL::FreeDB() {
	// statements must be finalized before the connection, or sqlite3_close fails with SQLITE_BUSY
	for (sqlite3_stmt** stmt : { &_skill_stmt, &_monster_stmt, &_map_stmt, &_buff_stmt }) {
		sqlite3_finalize(*stmt);
		*stmt = nullptr;
	}

	sqlite3_close(_db);
	_db = nullptr;
}

bool MySQL::InitSkillDB() {

	char* errbuf = nullptr;

	const char* sql = "CREATE TABLE IF NOT EXISTS Skill(Id intEGER PRIMARY KEY, Name_EN TEXT, Name_TC TEXT, Name_JP TEXT, Name_KR TEXT);";

	if (sqlite3_exec(_db, sql, 0, 0, &errbuf) != SQLITE_OK) {
		LogInstance.WriteLog("Error in InitSkillDB : %s", errbuf);
		sqlite3_free(errbuf);

		return FALSE;
	}

	std::string sql2 = std::string("SELECT Name_") + std::string(LANGMANAGER.GetText("STR_SQL_SUFFIX")) + " From Skill Where Id = ?";

	if (sqlite3_prepare_v2(_db, sql2.c_str(), -1, &_skill_stmt, 0) != SQLITE_OK) {
		LogInstance.WriteLog("Error in sqlite3_prepare_v2 : %s", sqlite3_errmsg(_db));

		return FALSE;
	}

	return TRUE;
}

bool MySQL::InitMonsterDB() {

	char* errbuf = nullptr;

	const char* sql = "CREATE TABLE IF NOT EXISTS Monster(Id intEGER PRIMARY KEY, Name_EN TEXT, Name_TC TEXT, Name_JP TEXT, Name_KR TEXT);";

	if (sqlite3_exec(_db, sql, 0, 0, &errbuf) != SQLITE_OK) {
		LogInstance.WriteLog("Error in InitMonsterDB : %s", errbuf);
		sqlite3_free(errbuf);

		return FALSE;
	}

	std::string sql2 = "SELECT Name_" + std::string(LANGMANAGER.GetText("STR_SQL_SUFFIX")) + ", type From Monster Where Id = ?";

	if (sqlite3_prepare_v2(_db, sql2.c_str(), -1, &_monster_stmt, 0) != SQLITE_OK) {
		LogInstance.WriteLog("Error in sqlite3_prepare_v2 : %s", sqlite3_errmsg(_db));

		return FALSE;
	}

	return TRUE;
}

bool MySQL::InitMapDB() {
	char* errbuf = nullptr;

	const char* sql = "CREATE TABLE IF NOT EXISTS Map(Id intEGER PRIMARY KEY, Name_EN TEXT, Name_TC TEXT, Name_JP TEXT, Name_KR TEXT);";

	if (sqlite3_exec(_db, sql, 0, 0, &errbuf) != SQLITE_OK) {
		LogInstance.WriteLog("Error in InitMapDB : %s", errbuf);
		sqlite3_free(errbuf);

		return FALSE;
	}

	std::string sql2 = "SELECT Name_" + std::string(LANGMANAGER.GetText("STR_SQL_SUFFIX")) + " From Map Where Id = ?";

	if (sqlite3_prepare_v2(_db, sql2.c_str(), -1, &_map_stmt, 0) != SQLITE_OK) {
		LogInstance.WriteLog("Error in sqlite3_prepare_v2 : %s", sqlite3_errmsg(_db));

		return FALSE;
	}

	return TRUE;
}

bool MySQL::InitBuffDB() {
	char* errbuf = nullptr;

	const char* sql = "CREATE TABLE IF NOT EXISTS Buff (Id intEGER PRIMARY KEY, Name_EN TEXT, Name_TC TEXT, Name_JP TEXT, Name_KR TEXT, Desc_EN TEXT, Desc_TC TEXT, Desc_JP TEXT, Desc_KR TEXT)";

	if (sqlite3_exec(_db, sql, 0, 0, &errbuf) != SQLITE_OK) {
		LogInstance.WriteLog("Error in InitBuffDB : %s", errbuf);
		sqlite3_free(errbuf);

		return FALSE;
	}

	std::string sql2 = "SELECT Name_" + std::string(LANGMANAGER.GetText("STR_SQL_SUFFIX")) + ",Desc_" + std::string(LANGMANAGER.GetText("STR_SQL_SUFFIX")) + " From Buff Where Id = ?";

	if (sqlite3_prepare_v2(_db, sql2.c_str(), -1, &_buff_stmt, 0) != SQLITE_OK) {
		LogInstance.WriteLog("Error in sqlite3_prepare_v2 : %s", sqlite3_errmsg(_db));

		return FALSE;
	}

	return TRUE;
}

bool MySQL::Init() {

	// re-run on language change: drop the previous connection and statements first
	FreeDB();

	bool success = TRUE;

	do {
		if (!InitDB()) {
			success = FALSE;
			break;
		}

		if (!InitSkillDB()) {
			success = FALSE;
			break;
		}

		if (!InitMonsterDB()) {
			success = FALSE;
			break;
		}

		if (!InitMapDB()) {
			success = FALSE;
			break;
		}

		if (!InitBuffDB()) {
			success = FALSE;
			break;
		}
	} while (FALSE);

	if (!success)
		FreeDB();

	return success;
}

bool MySQL::GetSkillName(uint32_t skillId, char* out_buffer, size_t out_buffer_length) {

	if (out_buffer == nullptr || _skill_stmt == nullptr)
		return FALSE;

	sprintf_s(out_buffer, out_buffer_length, "%d", skillId);

	sqlite3_reset(_skill_stmt);

	sqlite3_bind_int(_skill_stmt, 1, skillId);
	int step = sqlite3_step(_skill_stmt);

	if (step == SQLITE_ROW) {
		const char* result = (const char*)sqlite3_column_text(_skill_stmt, 0);

		if (result == nullptr || strlen(result) > out_buffer_length)
			return FALSE;

		strcpy_s(out_buffer, out_buffer_length, result);
	}

	return TRUE;
}

bool MySQL::GetMonsterName(uint32_t DB2, char* out_buffer, size_t out_buffer_length) {

	if (out_buffer == nullptr || _monster_stmt == nullptr)
		return FALSE;

	sprintf_s(out_buffer, out_buffer_length, "%d", DB2);

	sqlite3_reset(_monster_stmt);

	sqlite3_bind_int(_monster_stmt, 1, DB2);

	int step = sqlite3_step(_monster_stmt);

	if (step == SQLITE_ROW) {
		const char* result = (const char*)sqlite3_column_text(_monster_stmt, 0);

		if (result == nullptr || strlen(result) > out_buffer_length)
			return FALSE;

		strcpy_s(out_buffer, out_buffer_length, result);
	}

	return TRUE;
}

bool MySQL::GetMonsterType(uint32_t DB2, int32_t* type) {

	if (type == nullptr || _monster_stmt == nullptr)
		return FALSE;

	sqlite3_reset(_monster_stmt);

	sqlite3_bind_int(_monster_stmt, 1, DB2);

	int step = sqlite3_step(_monster_stmt);

	if (step == SQLITE_ROW) {
		int32_t result = sqlite3_column_int(_monster_stmt, 1);
		memcpy_s(type, sizeof(int32_t), &result, sizeof(int32_t));
	}

	return TRUE;
}

bool MySQL::GetMapName(uint32_t mapID, char* out_buffer, size_t out_buffer_length) {

	if (out_buffer == nullptr || _map_stmt == nullptr)
		return FALSE;

	if (mapID == 0) {
		strcpy_s(out_buffer, out_buffer_length, LANGMANAGER.GetText("STR_WORLD_NO_INFORMATION").data());
		return TRUE;
	}

	sprintf_s(out_buffer, out_buffer_length, "%d", mapID);

	sqlite3_reset(_map_stmt);

	sqlite3_bind_int(_map_stmt, 1, mapID);

	int step = sqlite3_step(_map_stmt);

	if (step == SQLITE_ROW) {
		const char* result = (const char*)sqlite3_column_text(_map_stmt, 0);

		if (result == nullptr || strlen(result) > out_buffer_length)
			return FALSE;

		strcpy_s(out_buffer, out_buffer_length, result);
	}

	return TRUE;
}

bool MySQL::GetBuffName(uint32_t buffId, char* out_buffer, size_t out_buffer_length, char* out_buffer_desc, size_t out_buffer_desc_length) {

	if (out_buffer == nullptr || _buff_stmt == nullptr)
		return FALSE;

	if (buffId == 0) {
		strcpy_s(out_buffer, out_buffer_length, const_cast<char*>(u8"Unknown"));
		return TRUE;
	}

	sprintf_s(out_buffer, out_buffer_length, "%d", buffId);

	sqlite3_reset(_buff_stmt);

	sqlite3_bind_int(_buff_stmt, 1, buffId);

	int step = sqlite3_step(_buff_stmt);

	if (step == SQLITE_ROW) {
		const char* result = (const char*)sqlite3_column_text(_buff_stmt, 0);

		if (result == nullptr || strlen(result) > out_buffer_length)
			return FALSE;

		strcpy_s(out_buffer, out_buffer_length, result);

		if (out_buffer_desc != NULL)
		{
			result = (const char*)sqlite3_column_text(_buff_stmt, 1);

			if (result == nullptr || strlen(result) > out_buffer_desc_length)
				return FALSE;

			strcpy_s(out_buffer_desc, out_buffer_desc_length, result);
		}
	}

	return TRUE;
}