#include "pch.h"
#include "UtillWindow.h"

void UtillWindow::OpenWindow()
{
	_isOpen = true;
}

void UtillWindow::handleDialogInfo()
{
	if (ImGui::FileDialog(&_fileDialogOpen, &_fileDialogInfo))
	{
		HISTORY.GetLock();
		{
			DAMAGEMETER.GetLock();
			{
				if (!DAMAGEMETER.isRun())
				{
					DAMAGEMETER.Clear();
					if (_fileDialogInfo.type == ImGuiFileDialogType_SaveFile)
					{
						if (HISTORY.size() > 0)
							SAVEDATA.Clone(_fileDialogInfo.resultPath.generic_string());
					}
					else if (_fileDialogInfo.type == ImGuiFileDialogType_OpenFile)
					{
						_currentIndex = -1;
						HISTORY.ClearVector();
						SAVEDATA.GetLock();
						{
							SAVEDATA.Reset();
							if (SAVEDATA.Init(_fileDialogInfo.resultPath.generic_string()))
							{
								SAVEDATA.Reset();
								if (SAVEDATA.Init())
								{
									LogInstance.WriteLog("[UtillWindow::Update] Load savedata failed");
									exit(1);
								}
								char label[256] = { 0 };
								ANSItoUTF8(std::string(LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_LOAD_FAILED")).data(), label, sizeof(label));
								MessageBoxA(UIWINDOW.GetHWND(), label, "ERROR", MB_ICONERROR | MB_OK);
							}
							SAVEDATA.FreeLock();
						}
					}
				}
				DAMAGEMETER.FreeLock();
			}
			HISTORY.FreeLock();
		}
	}
}

void UtillWindow::RefreshHistoryList()
{
	if (HISTORY.GetCurrentIndex() != _currentIndex && !HISTORY.isStop())
	{
		char label[1024] = { 0 };

		HISTORY.GetLock();
		{
			_currentIndex = HISTORY.GetCurrentIndex();
			DAMAGEMETER.SetCurrentHistoryId(-1);
			_historyTmp.clear();

			if (HISTORY.size() > 0) {
				int32_t i = static_cast<int32_t>(HISTORY.size());
				for (auto itr = HISTORY.rbegin(); itr != HISTORY.rend(); itr++)
				{
					HISTORY_INFO* hi = (HISTORY_INFO*)*itr;

					char mapName[MAX_MAP_LEN] = { 0 };
					SWDB.GetMapName(hi->_worldID, mapName, MAX_MAP_LEN);

					char extInfo[256] = { 0 };
					//if (hi->_historyData->_extInfo.length() > 0)
					//	sprintf_s(extInfo, "(%s)", hi->_historyData->_extInfo.c_str());
#ifdef _DEBUG
					sprintf_s(extInfo, "(M:%u)", hi->_worldID);
#endif

					char realClearTime[128] = { 0 };
					if (hi->_realClearTime > 0)
					{
						sprintf_s(realClearTime, "(%02d:%02d.%01d)",
							(unsigned int)hi->_realClearTime / 10 % 3600 / 60, (unsigned int)hi->_realClearTime / 10 % 3600 % 60, (unsigned int)hi->_realClearTime % 10
						);
					}

					sprintf_s(label, "%d.[%02d/%02d %02d:%02d:%02d] %s%s - %02d:%02d.%01d%s###history%d",
						i,
						hi->_saveTime->wMonth, hi->_saveTime->wDay, hi->_saveTime->wHour, hi->_saveTime->wMinute, hi->_saveTime->wSecond,
						extInfo,
						mapName,
						(unsigned int)hi->_time / (60 * 1000), (unsigned int)(hi->_time / 1000) % 60, (unsigned int)hi->_time % 1000 / 100,
						realClearTime,
						i
					);

					i--;

					_historyTmp.push_back(std::pair(hi, std::string(label)));
				}
			}

			HISTORY.FreeLock();
		}

		// A compare pick whose run was deleted or rotated out must not be dereferenced again;
		// the rest are re-read in case a freed run's address was reused by a new one.
		for (int side = 0; side < 2; side++)
		{
			if (_compareHI[side] == nullptr)
				continue;

			auto hit = std::find_if(_historyTmp.begin(), _historyTmp.end(), [&](const auto& h) { return h.first == _compareHI[side]; });
			if (hit == _historyTmp.end())
			{
				_compareHI[side] = nullptr;
				_compareSide[side] = COMPARE_SIDE();
			}
			else
				BuildCompareSide(side);
		}
	}
}

void UtillWindow::HistoryWindow()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_MENU_HISTORY").data()))
	{
		char label[1024] = { 0 };

		ImGui::InputText(LANGMANAGER.GetText("STR_UTILLWINDOW_SEARCH").data(), _searchData, IM_ARRAYSIZE(_searchData));

		if (UIOPTION.isUseSaveData())
		{
			sprintf_s(label, "%s(%s %d) : %s", LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_USING").data(), LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_MAX").data(), HISTORY_SIZE, SAVEDATA.CurrentSaveDataPath().c_str());
		}
		else {
			sprintf_s(label, "%s(%s %d)", LANGMANAGER.GetText("STR_MENU_HISTORY").data(), LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_MAX").data(), HISTORY_SIZE);
		}
		ImGui::Text(label);

		if (UIOPTION.isUseSaveData())
		{
			if (ImGui::Button(LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_LOAD").data()))
			{
				if (!DAMAGEMETER.isRun())
				{
					_fileDialogOpen = true;
					_fileDialogInfo.type = ImGuiFileDialogType_OpenFile;
					_fileDialogInfo.title = "Load SoulMeter savedata###FileDiglogLoad";
					_fileDialogInfo.fileName = ".dat";
					_fileDialogInfo.directoryPath = std::filesystem::current_path();
				}
			}

			handleDialogInfo();

			ImGui::SameLine();
			if (ImGui::Button(LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_SAVETO").data()))
			{
				if (!DAMAGEMETER.isRun() && HISTORY.size() > 0)
				{
					_fileDialogOpen = true;
					_fileDialogInfo.type = ImGuiFileDialogType_SaveFile;
					_fileDialogInfo.title = "Save SoulMeter savedata###FileDiglogSave";
					_fileDialogInfo.fileName = ".dat";
					_fileDialogInfo.directoryPath = std::filesystem::current_path();
				}
			}
		}

		ImGui::SameLine();
		if (ImGui::Button(LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_CLEARALL").data()))
		{
			HISTORY.GetLock();
			{
				DAMAGEMETER.GetLock();
				{
					ANSItoUTF8(std::string(LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_CLEARALL_CONFIRM")).data(), label, sizeof(label));
					if (!DAMAGEMETER.isRun() && HISTORY.size() > 0 && MessageBoxA(UIWINDOW.GetHWND(), label, "WARNING", MB_ICONWARNING | MB_YESNO | MB_TOPMOST) == IDYES)
					{
						DAMAGEMETER.Clear();
						HISTORY.ClearAll();
						_currentIndex = -1;
					}
					DAMAGEMETER.FreeLock();
				}
				HISTORY.FreeLock();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button(LANGMANAGER.GetText("STR_UTILLWINDOW_HISTORY_DELETE_SELECTED").data()))
		{
			HISTORY.GetLock();
			{
				DAMAGEMETER.GetLock();
				{
					if (!DAMAGEMETER.isRun() && DAMAGEMETER.isHistoryMode() && DAMAGEMETER.GetCurrentHistoryId() >= 0)
					{
						HISTORY_INFO* HI = (HISTORY_INFO*)DAMAGEMETER.GetHistoryHI();
						DAMAGEMETER.Clear();

						HISTORY.ClearHistory(HI, FALSE);
						_currentIndex = -1;
					}
					DAMAGEMETER.FreeLock();
				}
				HISTORY.FreeLock();
			}
		}

		RefreshHistoryList();

		ImGui::BeginChild("select history", ImVec2(0, 0), true);
		{
			int32_t i = static_cast<int32_t>(_historyTmp.size());
			for (auto itr = _historyTmp.begin(); itr != _historyTmp.end(); itr++)
			{
				if (itr->first == nullptr)
					continue;
				if (strlen(_searchData) > 0 && itr->second.find(std::string(_searchData)) == std::string::npos)
					continue;

				if (ImGui::Selectable(itr->second.c_str(), DAMAGEMETER.GetHistoryHI() == itr->first) && !DAMAGEMETER.isRun()) {
					if (!DAMAGEMETER.isRun()) {
						DAMAGEMETER.Clear();
						DAMAGEMETER.SetCurrentHistoryId(i);
						DAMAGEMETER.SetHistory((LPVOID)itr->first);
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::EndTabItem();
	}
}

static const ImVec4 kCompareBetter = ImVec4(0.40f, 0.90f, 0.40f, 1.0f);
static const ImVec4 kCompareWorse = ImVec4(0.95f, 0.45f, 0.45f, 1.0f);

// Same unit handling as the damage table, so the two read alike.
static void CompareFormatDamage(double damage, char* dest, size_t destLen)
{
	char label[128] = { 0 };
	std::string unit;

	if (UIOPTION.is1K()) {
		damage /= 1000;
		unit = LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K");
	}
	else if (UIOPTION.is1M()) {
		damage /= 1000000;
		unit = LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M");
	}
	else if (UIOPTION.is10K()) {
		damage /= 10000;
		unit = LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K");
	}

	sprintf_s(label, "%.0f", damage);
	TextCommma(label, dest);
	strcat_s(dest, destLen, unit.c_str());
}

static void CompareFormatTime(uint64_t ms, char* dest, size_t destLen)
{
	sprintf_s(dest, destLen, "%02u:%02u.%01u", (unsigned int)(ms / (60 * 1000)), (unsigned int)(ms / 1000 % 60), (unsigned int)(ms % 1000 / 100));
}

// better: 1 = green when B > A, -1 = green when B < A, 0 = uncoloured.
static void CompareDiffText(double diff, const char* fmt, int better)
{
	char label[128] = { 0 };
	sprintf_s(label, fmt, diff);

	if (better == 0 || fabs(diff) < 0.05)
		ImGui::TextUnformatted(label);
	else
		ImGui::TextColored(((diff > 0) == (better > 0)) ? kCompareBetter : kCompareWorse, "%s", label);
}

static double CompareCritRate(uint32_t crit, uint32_t hit)
{
	return hit == 0 ? 0.0 : (double)crit / hit * 100.0;
}

void UtillWindow::BuildCompareSide(int side)
{
	COMPARE_SIDE& out = _compareSide[side];
	out = COMPARE_SIDE();

	HISTORY_INFO* hi = _compareHI[side];
	if (hi == nullptr)
		return;

	HISTORY.GetLock();
	{
		DAMAGEMETER.GetLock();
		{
			// The pick is only a pointer; check it is still a live run before touching it.
			bool alive = std::find(HISTORY.begin(), HISTORY.end(), (LPVOID)hi) != HISTORY.end();

			if (!alive || hi->_historyData == nullptr)
				_compareHI[side] = nullptr;
			else
			{
				HISTORY_DATA* hd = hi->_historyData;
				SWDamagePlayer* player = nullptr;

				for (auto itr = hd->_playerHistory.begin(); itr != hd->_playerHistory.end(); itr++)
				{
					uint32_t id = (*itr)->GetID();
					std::string name;

					if (id == hi->_myID)
						name = LANGMANAGER.GetText("STR_TABLE_YOU");
					else
					{
						auto meta = hd->_playerMetadata.find(id);
						if (meta != hd->_playerMetadata.end())
							name = meta->second->_name;
						else
							name = LANGMANAGER.GetText("PLAYER_NAME_CANT_FIND");
					}
					out._players.push_back(std::pair(id, name));

					if (id == _comparePlayer[side])
						player = *itr;
				}

				// Fall back to yourself, then to whoever is first, when the picked id is not in this run.
				for (auto itr = hd->_playerHistory.begin(); player == nullptr && itr != hd->_playerHistory.end(); itr++)
					if ((*itr)->GetID() == hi->_myID)
						player = *itr;
				if (player == nullptr && !hd->_playerHistory.empty())
					player = hd->_playerHistory.front();

				if (player != nullptr)
				{
					out._valid = true;
					out._playerID = player->GetID();
					out._isMe = out._playerID == hi->_myID;
					out._time = hi->_time;
					out._damage = player->GetDamage();
					out._hitCountForCritRate = player->GetHitCountForCritRate();
					out._critHitCountForCritRate = player->GetCritHitCountForCritRate();
					out._casts = player->GetSkillUsed();
					out._deaths = player->GetDeathCount();
					out._avgAB = player->GetHistoryAvgAB();
					out._avgBD = player->GetHistoryAvgBD();
					out._fullABTime = player->GetHistoryABTime();

					_comparePlayer[side] = out._playerID;

					for (auto monster = player->begin(); monster != player->end(); monster++)
					{
						for (auto skill = (*monster)->begin(); skill != (*monster)->end(); skill++)
						{
							COMPARE_SKILL& row = out._skills[(*skill)->GetID()];
							if (row._name.empty())
								row._name = (*skill)->GetName();

							row._damage += (*skill)->GetDamage();
							row._hitCount += (*skill)->GetHitCount();
							row._critHitCount += (*skill)->GetCritHitCount();

							out._skillDamage += (*skill)->GetDamage();
						}
					}

					for (auto itr = player->skillCounts.begin(); itr != player->skillCounts.end(); itr++)
					{
						COMPARE_SKILL& row = out._skills[itr->first];
						row._casts = itr->second->_count;

						if (row._name.empty())
						{
							char skillName[SKILL_NAME_LEN] = { 0 };
							SWDB.GetSkillName(itr->first, skillName, SKILL_NAME_LEN);
							row._name = skillName;
						}
					}
				}
			}
			DAMAGEMETER.FreeLock();
		}
		HISTORY.FreeLock();
	}
}

void UtillWindow::CompareSummaryTable()
{
	const COMPARE_SIDE& a = _compareSide[0];
	const COMPARE_SIDE& b = _compareSide[1];

	char label[256] = { 0 };

	if (!ImGui::BeginTable("##comparesummary", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
		return;

	ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_METRIC").data(), ImGuiTableColumnFlags_NoHide);
	ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_RUN_A").data());
	ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_RUN_B").data());
	sprintf_s(label, "%s (B-A)", LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_DIFF").data());
	ImGui::TableSetupColumn(label);
	ImGui::TableHeadersRow();

	auto row = [](const char* key) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(LANGMANAGER.GetText(key).data());
		ImGui::TableNextColumn();
	};

	// Clear time
	row("STR_UTILLWINDOW_COMPARE_CLEAR_TIME");
	CompareFormatTime(a._time, label, sizeof(label));
	ImGui::TextUnformatted(label);
	ImGui::TableNextColumn();
	CompareFormatTime(b._time, label, sizeof(label));
	ImGui::TextUnformatted(label);
	ImGui::TableNextColumn();
	CompareDiffText(((double)b._time - (double)a._time) / 1000.0, "%+.1fs", -1);

	// DPS and total damage, diff as percent of A
	double dpsA = a._time == 0 ? 0.0 : (double)a._damage / ((double)a._time / 1000.0);
	double dpsB = b._time == 0 ? 0.0 : (double)b._damage / ((double)b._time / 1000.0);
	struct { const char* key; double va; double vb; } amounts[] = {
		{ "STR_TABLE_DPS", dpsA, dpsB },
		{ "STR_TABLE_TOTAL_DAMAGE", (double)a._damage, (double)b._damage },
	};
	for (auto& m : amounts)
	{
		row(m.key);
		CompareFormatDamage(m.va, label, sizeof(label));
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
		CompareFormatDamage(m.vb, label, sizeof(label));
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
		if (m.va > 0)
			CompareDiffText((m.vb - m.va) / m.va * 100.0, "%+.1f%%", 1);
		else
			ImGui::TextUnformatted("-");
	}

	// Crit rate
	double critA = CompareCritRate(a._critHitCountForCritRate, a._hitCountForCritRate);
	double critB = CompareCritRate(b._critHitCountForCritRate, b._hitCountForCritRate);
	row("STR_TABLE_CRIT_RATE");
	ImGui::Text("%.1f", critA);
	ImGui::TableNextColumn();
	ImGui::Text("%.1f", critB);
	ImGui::TableNextColumn();
	CompareDiffText(critB - critA, "%+.1f", 1);

	// AB / BD are only recorded for your own character, like in the main table.
	double fullABA = a._time == 0 ? 0.0 : a._fullABTime * 1000.0 / a._time * 100.0;
	double fullABB = b._time == 0 ? 0.0 : b._fullABTime * 1000.0 / b._time * 100.0;
	struct { const char* key; double va; double vb; } uptime[] = {
		{ "STR_TABLE_AVERAGE_AB", a._avgAB, b._avgAB },
		{ "STR_TABLE_FULL_AB_PERCENT", fullABA, fullABB },
		{ "STR_TABLE_AVERAGE_BD", a._avgBD, b._avgBD },
	};
	for (auto& u : uptime)
	{
		row(u.key);
		if (a._isMe) ImGui::Text("%.1f", u.va); else ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();
		if (b._isMe) ImGui::Text("%.1f", u.vb); else ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();
		if (a._isMe && b._isMe)
			CompareDiffText(u.vb - u.va, "%+.1f", 1);
		else
			ImGui::TextUnformatted("-");
	}

	// Casts
	row("STR_SPECIFICINFO_USE_SKILL_COUNTS");
	ImGui::Text("%u", a._casts);
	ImGui::TableNextColumn();
	ImGui::Text("%u", b._casts);
	ImGui::TableNextColumn();
	CompareDiffText((double)b._casts - (double)a._casts, "%+.0f", 0);

	// Deaths
	row("STR_TABLE_DEATH");
	ImGui::Text("%u", a._deaths);
	ImGui::TableNextColumn();
	ImGui::Text("%u", b._deaths);
	ImGui::TableNextColumn();
	CompareDiffText((double)b._deaths - (double)a._deaths, "%+.0f", -1);

	ImGui::EndTable();
}

void UtillWindow::CompareSkillTable()
{
	const COMPARE_SIDE& a = _compareSide[0];
	const COMPARE_SIDE& b = _compareSide[1];

	struct ROW {
		uint32_t id;
		const COMPARE_SKILL* a;
		const COMPARE_SKILL* b;
		double shareA;
		double shareB;
	};
	std::vector<ROW> rows;

	auto share = [](const COMPARE_SKILL* s, uint64_t total) { return (s == nullptr || total == 0) ? 0.0 : (double)s->_damage / total * 100.0; };

	for (auto& s : a._skills)
	{
		auto other = b._skills.find(s.first);
		const COMPARE_SKILL* sb = other != b._skills.end() ? &other->second : nullptr;
		rows.push_back({ s.first, &s.second, sb, share(&s.second, a._skillDamage), share(sb, b._skillDamage) });
	}
	for (auto& s : b._skills)
	{
		if (a._skills.find(s.first) == a._skills.end())
			rows.push_back({ s.first, nullptr, &s.second, 0.0, share(&s.second, b._skillDamage) });
	}

	std::sort(rows.begin(), rows.end(), [](const ROW& x, const ROW& y) {
		double mx = (std::max)(x.shareA, x.shareB), my = (std::max)(y.shareA, y.shareB);
		if (mx != my)
			return mx > my;
		uint32_t cx = (std::max)(x.a ? x.a->_casts : 0u, x.b ? x.b->_casts : 0u);
		uint32_t cy = (std::max)(y.a ? y.a->_casts : 0u, y.b ? y.b->_casts : 0u);
		return cx > cy;
	});

	char label[256] = { 0 };
	std::string diff(LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_DIFF"));

	ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
	if (!ImGui::BeginTable("##compareskills", 10, flags))
		return;

	ImGui::TableSetupScrollFreeze(1, 1);
	ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_SPECIFICINFO_SKILL").data(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, 180.0f);

	const char* groups[] = { "STR_TABLE_DAMAGE_PERCENT", "STR_SPECIFICINFO_USE_SKILL_COUNTS", "STR_TABLE_CRIT_RATE" };
	for (const char* g : groups)
	{
		std::string name(LANGMANAGER.GetText(g));
		sprintf_s(label, "%s A##%s_a", name.c_str(), g);
		ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed);
		sprintf_s(label, "%s B##%s_b", name.c_str(), g);
		ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed);
		sprintf_s(label, "%s##%s_d", diff.c_str(), g);
		ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed);
	}
	ImGui::TableHeadersRow();

	for (auto& r : rows)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		const COMPARE_SKILL* any = r.a != nullptr ? r.a : r.b;
		ImGui::TextUnformatted(any->_name.c_str());
		if (ImGui::IsItemHovered())
		{
			char dmgA[128] = { 0 }, dmgB[128] = { 0 };
			CompareFormatDamage(r.a ? (double)r.a->_damage : 0.0, dmgA, sizeof(dmgA));
			CompareFormatDamage(r.b ? (double)r.b->_damage : 0.0, dmgB, sizeof(dmgB));
			ImGui::SetTooltip("%s (%u)\nA: %s\nB: %s", any->_name.c_str(), r.id, dmgA, dmgB);
		}

		// Damage share
		ImGui::TableNextColumn();
		ImGui::Text("%.1f", r.shareA);
		ImGui::TableNextColumn();
		ImGui::Text("%.1f", r.shareB);
		ImGui::TableNextColumn();
		CompareDiffText(r.shareB - r.shareA, "%+.1f", 0);

		// Casts
		uint32_t castsA = r.a ? r.a->_casts : 0, castsB = r.b ? r.b->_casts : 0;
		ImGui::TableNextColumn();
		ImGui::Text("%u", castsA);
		ImGui::TableNextColumn();
		ImGui::Text("%u", castsB);
		ImGui::TableNextColumn();
		CompareDiffText((double)castsB - (double)castsA, "%+.0f", 0);

		// Crit rate, only where the skill actually hit
		bool hitA = r.a && r.a->_hitCount > 0, hitB = r.b && r.b->_hitCount > 0;
		double critA = hitA ? CompareCritRate(r.a->_critHitCount, r.a->_hitCount) : 0.0;
		double critB = hitB ? CompareCritRate(r.b->_critHitCount, r.b->_hitCount) : 0.0;
		ImGui::TableNextColumn();
		if (hitA) ImGui::Text("%.1f", critA); else ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();
		if (hitB) ImGui::Text("%.1f", critB); else ImGui::TextUnformatted("-");
		ImGui::TableNextColumn();
		if (hitA && hitB)
			CompareDiffText(critB - critA, "%+.1f", 1);
		else
			ImGui::TextUnformatted("-");
	}

	ImGui::EndTable();
}

void UtillWindow::CompareWindow()
{
	if (!ImGui::BeginTabItem(LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE").data()))
		return;

	RefreshHistoryList();

	char label[1024] = { 0 };
	const char* runKeys[2] = { "STR_UTILLWINDOW_COMPARE_RUN_A", "STR_UTILLWINDOW_COMPARE_RUN_B" };

	for (int side = 0; side < 2; side++)
	{
		ImGui::PushID(side);

		std::string preview;
		for (auto& h : _historyTmp)
		{
			if (h.first == _compareHI[side])
			{
				preview = h.second.substr(0, h.second.find("###"));
				break;
			}
		}

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
		sprintf_s(label, "%s###comparerun", LANGMANAGER.GetText(runKeys[side]).data());
		if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge))
		{
			for (auto& h : _historyTmp)
			{
				if (h.first == nullptr)
					continue;

				if (ImGui::Selectable(h.second.c_str(), h.first == _compareHI[side]) && h.first != _compareHI[side])
				{
					_compareHI[side] = h.first;
					BuildCompareSide(side);
				}
			}
			ImGui::EndCombo();
		}

		if (_compareSide[side]._valid)
		{
			std::string playerName;
			for (auto& p : _compareSide[side]._players)
				if (p.first == _compareSide[side]._playerID)
					playerName = p.second;

			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			sprintf_s(label, "%s###compareplayer", LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_PLAYER").data());
			if (ImGui::BeginCombo(label, playerName.c_str()))
			{
				for (auto& p : _compareSide[side]._players)
				{
					sprintf_s(label, "%s##%u", p.second.c_str(), p.first);
					if (ImGui::Selectable(label, p.first == _compareSide[side]._playerID) && p.first != _compareSide[side]._playerID)
					{
						_comparePlayer[side] = p.first;
						BuildCompareSide(side);
					}
				}
				ImGui::EndCombo();
			}
		}

		ImGui::PopID();
	}

	if (!_compareSide[0]._valid || !_compareSide[1]._valid)
	{
		ImGui::TextDisabled("%s", LANGMANAGER.GetText("STR_UTILLWINDOW_COMPARE_PICK").data());
	}
	else
	{
		CompareSummaryTable();
		ImGui::Spacing();
		CompareSkillTable();
	}

	ImGui::EndTabItem();
}

void UtillWindow::CombatWindow()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_UTILLWINDOW_COMBAT").data()))
	{
		char label[1024] = { 0 };
		_mutex.lock();
		{
			auto combatIF = COMBATMETER.Get();

			if (combatIF != nullptr)
			{
				if (_ci == nullptr)
				{
					COMBATMETER.GetLock();
					{
						if (combatIF->begin() != combatIF->end())
							_ci = combatIF->begin()->second;

						COMBATMETER.FreeLock();
					}
				}

				if (_ci != nullptr)
				{
					ImGui::InputText(LANGMANAGER.GetText("STR_UTILLWINDOW_SEARCH").data(), _searchData2, IM_ARRAYSIZE(_searchData2));

					sprintf_s(label, "%s###UtillCombatSelector", LANGMANAGER.GetText("STR_UTILLWINDOW_COMBAT_SELECTOR").data());
					if (ImGui::BeginCombo(label, COMBATMETER.GetName(_ci).c_str(), ImGuiComboFlags_HeightLarge))
					{
						COMBATMETER.GetLock();
						{
							for (auto itr = combatIF->begin(); itr != combatIF->end(); itr++)
							{
								sprintf_s(label, "%s##%d", COMBATMETER.GetName(itr->second).c_str(), itr->first);

								if (ImGui::Selectable(label, _ci == itr->second)) {
									_ci = itr->second;
									ForceUpdateCombatTemp(_ci);
								}
							}
							COMBATMETER.FreeLock();
						}
						ImGui::EndCombo();
					}

					ImGui::BeginChild("display combat log", ImVec2(0, 0), true);
					{
						if (_ci->size() != _combatTmp.size())
						{
							COMBATMETER.GetLock();
							{
								ForceUpdateCombatTemp(_ci);
								COMBATMETER.FreeLock();
							}
						}

						for (auto itr = _combatTmp.begin(); itr != _combatTmp.end(); itr++)
						{
							const char* label = itr->second.c_str();

							if (strlen(_searchData2) > 0 && std::string(label).find(std::string(_searchData2)) == std::string::npos)
								continue;

							ImGui::Text(label);
						}
						ImGui::EndChild();
					}
				}
			}
			_mutex.unlock();
		}
		ImGui::EndTabItem();
	}
}

void UtillWindow::ForceUpdateCombatTemp(Combat* pCombat)
{
	_combatTmp.clear();

	char label[1024] = { 0 };
	size_t i = pCombat->size();
	int j = 0;
	for (auto itr2 = pCombat->rbegin(); itr2 != pCombat->rend(); itr2++)
	{
		CombatLog* pCombatLog = itr2->second;
		SYSTEMTIME* time = pCombatLog->_time;

		sprintf_s(label, "%llu.[%02d:%02d:%02d.%03d] %s",
			i--,
			time->wHour, time->wMinute, time->wSecond, time->wMilliseconds,
			COMBATMETER.ConvertCombatLogVal(pCombatLog, (CombatType)pCombat->GetType()).c_str()
		);

		_combatTmp.push_back(std::pair(j++, std::string(label)));
	}
}

void UtillWindow::ClearCombatTemp()
{
	_mutex.lock();
	{
		_ci = nullptr;
		_combatTmp.clear();
		_mutex.unlock();
	}
}

void UtillWindow::Update()
{
	if (!_isOpen)
		return;

	char label[1024] = { 0 };
	sprintf_s(label, "%s###UtillWindow", LANGMANAGER.GetText("STR_MENU_UTILL").data());

	// Auto-fit on first open leaves the history list a sliver.
	const float em = ImGui::GetFontSize();
	ImGui::SetNextWindowSize(ImVec2(em * 28.0f, em * 18.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin(label, &_isOpen, ImGuiWindowFlags_None);
	{
		if (ImGui::BeginTabBar(u8"UtillWindowTab"))
		{
			HistoryWindow();
			CompareWindow();
			CombatWindow();
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

UtillWindow::UtillWindow()
{
}

UtillWindow::~UtillWindow()
{
	bool a = _mutex.try_lock();
	_mutex.unlock();
}
