#pragma once
#include "pch.h"
#include ".\FlatBuffers\include\SW_HISTORY_.h"
#include ".\Damage Meter\History.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\Damage Meter\SaveData.h"
#include ".\UI\UiWindow.h"
#include ".\Combat Meter\CombatMeter.h"
#include ".\Combat Meter\Combat.h"


using namespace SoulMeterFBS::History;

#define UTILLWINDOW UtillWindow::getInstance()

struct COMPARE_SKILL {
	std::string _name;
	uint64_t _damage = 0;
	uint32_t _hitCount = 0;
	uint32_t _critHitCount = 0;
	uint32_t _casts = 0;
};

struct COMPARE_SIDE {
	bool _valid = false;
	bool _isMe = false;
	uint32_t _playerID = 0;
	uint64_t _time = 0;
	uint64_t _damage = 0;
	uint64_t _skillDamage = 0;
	uint32_t _hitCountForCritRate = 0;
	uint32_t _critHitCountForCritRate = 0;
	uint32_t _casts = 0;
	uint32_t _deaths = 0;
	double _avgAB = 0;
	double _avgBD = 0;
	double _fullABTime = 0;
	std::map<uint32_t, COMPARE_SKILL> _skills;
	std::vector<std::pair<uint32_t, std::string>> _players;
};

class UtillWindow : public Singleton<UtillWindow> {
private:
	bool _isOpen = false;

	ImFileDialogInfo _fileDialogInfo;
	bool _fileDialogOpen = false;

	char _searchData[MAX_PATH] = { 0 };
	char _searchData2[MAX_PATH] = { 0 };
	int32_t _currentIndex = 0;

	Combat* _ci = nullptr;
	std::vector<std::pair<uint32_t, std::string>> _combatTmp;
	std::mutex _mutex;

	std::vector<std::pair<HISTORY_INFO*, std::string>> _historyTmp;

	HISTORY_INFO* _compareHI[2] = { nullptr, nullptr };
	uint32_t _comparePlayer[2] = { 0, 0 };
	COMPARE_SIDE _compareSide[2];

	void handleDialogInfo();
	void RefreshHistoryList();
	void HistoryWindow();

	void CompareWindow();
	void BuildCompareSide(int side);
	void CompareSummaryTable();
	void CompareSkillTable();

	void CombatWindow();
	void ForceUpdateCombatTemp(Combat* pCombat);
public:

	void OpenWindow();
	void Update();

	UtillWindow();
	~UtillWindow();

	void ClearCombatTemp();
};