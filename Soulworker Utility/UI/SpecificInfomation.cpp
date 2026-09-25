#include "pch.h"
#include ".\UI\SpecificInfomation.h"
#include ".\UI\Option.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\UI\UiWindow.h"
#include ".\Damage Meter\MySQLite.h"

SpecificInformation::SpecificInformation(uint32_t playerID) : _playerID(playerID), _monsterID_SKILL(0), _monsterFilter(MonsterFilter::Single), _customWindowOpen(false), _comboPreview(), _skillTotalsDamage(0), _globalFontScale(0), _columnFontScale(0), _tableFontScale(0), _tableTime(0), _accumulatedTime(0) {
}

SpecificInformation::~SpecificInformation() {
	Clear();
}

void SpecificInformation::Clear() {
}

void SpecificInformation::SetPlayerID(uint32_t playerID) {

	if (_playerID == playerID)
		return;

	_playerID = playerID;
	ResetMonsterSelection();
}

void SpecificInformation::ResetMonsterSelection() {
	_monsterID_SKILL = 0;

	// Monster ids are per-run, so a subset picked for the previous run means
	// nothing here - drop it and fall back to the auto-latching single pick.
	_customMonsters.clear();

	if (_monsterFilter == MonsterFilter::Custom)
		_monsterFilter = MonsterFilter::Single;
}

bool SpecificInformation::IsMonsterSelected(uint32_t monsterID) const {

	switch (_monsterFilter) {
	case MonsterFilter::All:
		return true;
	case MonsterFilter::Custom:
		return _customMonsters.find(monsterID) != _customMonsters.end();
	default:
		return monsterID == _monsterID_SKILL;
	}
}

void SpecificInformation::BuildSkillTotals() {

	_skillTotals.clear();
	_skillTotalsDamage = 0;

	auto player = DAMAGEMETER.GetPlayerInfo(_playerID);

	if (player == DAMAGEMETER.end())
		return;

	// Every monster is walked, not just the selected ones: the TOTAL column is
	// the skill's whole output for the run and must not follow the selection.
	for (auto monster = (*player)->begin(); monster != (*player)->end(); monster++) {

		bool selected = IsMonsterSelected((*monster)->GetID());

		for (auto skill = (*monster)->begin(); skill != (*monster)->end(); skill++) {

			uint32_t skillID = (*skill)->GetID();

			auto row = std::find_if(_skillTotals.begin(), _skillTotals.end(), [skillID](const SKILL_TOTAL& total) { return total._id == skillID; });

			if (row == _skillTotals.end()) {
				_skillTotals.push_back({ skillID, (*skill)->GetName(), 0, 0, 0, 0, false });
				row = _skillTotals.end() - 1;
			}

			row->_totalDamage += (*skill)->GetDamage();

			if (!selected)
				continue;

			row->_inSelection = true;
			row->_damage += (*skill)->GetDamage();
			row->_hitCount += (*skill)->GetHitCount();
			row->_critHitCount += (*skill)->GetCritHitCount();

			_skillTotalsDamage += (*skill)->GetDamage();
		}
	}

	// Skills only ever used on deselected mobs have no row in this breakdown.
	_skillTotals.erase(std::remove_if(_skillTotals.begin(), _skillTotals.end(), [](const SKILL_TOTAL& total) { return !total._inSelection; }), _skillTotals.end());

	std::sort(_skillTotals.begin(), _skillTotals.end(), [](const SKILL_TOTAL& a, const SKILL_TOTAL& b) { return a._damage > b._damage; });
}

void SpecificInformation::FormatDamage(uint64_t damage, char* dest, size_t destLen) {

	char label[128] = { 0 };

	if (UIOPTION.is1K())
		damage /= 1000;
	else if (UIOPTION.is1M())
		damage /= 1000000;
	else if (UIOPTION.is10K())
		damage /= 10000;

	sprintf_s(label, 128, "%llu", damage);
	TextCommma(label, dest);

	if (UIOPTION.is1K())
		strcat_s(dest, destLen, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
	else if (UIOPTION.is1M())
		strcat_s(dest, destLen, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M").data());
	else if (UIOPTION.is10K())
		strcat_s(dest, destLen, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
}

void SpecificInformation::SetupFontScale() {
	ImFont* font = ImGui::GetFont();

	_globalFontScale = font->Scale;
	_columnFontScale = _globalFontScale * UIOPTION.GetColumnFontScale();
	_tableFontScale = _globalFontScale * UIOPTION.GetTableFontScale();
}

void SpecificInformation::Update(bool* open, int64_t index) {

	SetupFontScale();

	_accumulatedTime += UIWINDOW.GetDeltaTime();

	if (_accumulatedTime > UIOPTION.GetRefreshTime()) {
		_tableTime = static_cast<float>((double)DAMAGEMETER.GetTime() / 1000);
		_accumulatedTime = 0;
	}

	char title[128] = { 0 };

	sprintf_s(title, 128, "%s %s ###SpecificInformation%lld", DAMAGEMETER.GetPlayerName(_playerID), LANGMANAGER.GetText("STR_SPECIFICINFO_DETAIL").data(), index);
	// Fitting to the first frame's content leaves the skill table squeezed.
	const float em = ImGui::GetFontSize();
	ImGui::SetNextWindowSize(ImVec2(em * 24.0f, em * 14.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin(title, (bool*)open, ImGuiWindowFlags_None);
	{
		sprintf_s(title, 128, "##tab");

		if(ImGui::BeginTabBar(title))
		{
			UpdateSkillInfo();
			UpdateSkillTotalInfo();
			UpdateBuffMeter();

			ImGui::EndTabBar();
		}
	}
	ImGui::End();

	UpdateMonsterFilterWindow(index);
}

void SpecificInformation::UpdateSkillInfo() {
	
	char label[128] = { 0 };
	sprintf_s(label, "%s###DetailSkill", LANGMANAGER.GetText("STR_SPECIFICINFO_SKILL").data());

	if(ImGui::BeginTabItem(label))
	{
		UpdateMonsterCombo();

		bool textEffect = THEME.PushTextEffect();
		ImGui::TextAlignCenter::SetTextAlignCenter();
		{
			UpdateSkillTable();
		}
		ImGui::TextAlignCenter::UnSetTextAlignCenter();
		THEME.PopTextEffect(textEffect);

		ImGui::EndTabItem();
	}
}

void SpecificInformation::UpdateSkillTotalInfo() {

	char label[128] = { 0 };
	sprintf_s(label, "%s###SpecificinfoTotal", LANGMANAGER.GetText("STR_SPECIFICINFO_TOTAL").data());
	if (ImGui::BeginTabItem(label))
	{
		bool textEffect = THEME.PushTextEffect();
		ImGui::TextAlignCenter::SetTextAlignCenter();
		{
			UpdateSkillTotalTable();
		}
		ImGui::TextAlignCenter::UnSetTextAlignCenter();
		THEME.PopTextEffect(textEffect);

		ImGui::EndTabItem();
	}
}

void SpecificInformation::UpdateSkillTotalTable()
{
	auto player = DAMAGEMETER.GetPlayerInfo(_playerID);

	if (player == DAMAGEMETER.end())
		return;

	ImGuiStyle& style = ImGui::GetStyle();

	ImVec2 prevWindowPadding = style.WindowPadding;
	style.WindowPadding.x = 0;
	style.WindowPadding.y = 0;

	char table[128] = { 0 };
	sprintf_s(table, 128, "##skilltotaltable");
	if (ImGui::BeginTable(table, 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | THEME.TableFlags())) {

		ImGui::SetWindowFontScale(_columnFontScale);

		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_NAME").data(), ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoClip | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_SPECIFICINFO_USE_SKILL_COUNTS").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_SPECIFICINFO_USE_SKILL_COUNTS_IN_FULL_AB").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableHeadersRow();

		char comma[128] = { 0 }; char label[128] = { 0 };
		float windowWidth = ImGui::GetWindowWidth();

		ImGui::SetWindowFontScale(_tableFontScale);
		for (auto itr = (*player)->skillCounts.begin(); itr != (*player)->skillCounts.end(); itr++) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			// NAME
			SWDB.GetSkillName(itr->first, _skillName, SKILL_NAME_LEN);
			ImGui::Text(_skillName);
			ImGui::TableNextColumn();

			// 시전횟수
			sprintf_s(label, 128, "%d", itr->second->_count);
			TextCommma(label, comma);
			ImGui::Text(comma);
			ImGui::TableNextColumn();

			// In Full AB
			if ((*player)->GetID() == DAMAGEMETER.GetMyID(TRUE))
			{
				sprintf_s(label, 128, "%d", itr->second->_in_full_ab_count);
			}
			else {
				sprintf_s(label, 128, "-");
			}
			TextCommma(label, comma);
			ImGui::Text(comma);
		}
		ImGui::EndTable();
	}
	ImGui::SetWindowFontScale(_globalFontScale);

	style.WindowPadding.x = prevWindowPadding.x;
	style.WindowPadding.y = prevWindowPadding.y;
}

void SpecificInformation::UpdateMonsterCombo() {

	auto player = DAMAGEMETER.GetPlayerInfo(_playerID);

	if (player == DAMAGEMETER.end())
		return;

	_comboPreview[0] = 0;

	if (_monsterFilter == MonsterFilter::All)
		strcpy_s(_comboPreview, LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_ALL").data());
	else if (_monsterFilter == MonsterFilter::Custom)
		sprintf_s(_comboPreview, "%s (%zu)", LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_CUSTOM").data(), _customMonsters.size());
	else {
		auto monster = (*player)->GetMonsterInfo(_monsterID_SKILL);

		// Nothing picked yet, or the pick died with the previous run: latch onto
		// the first mob that got hit so the table is filled without a manual pick.
		if (monster == (*player)->end() && (*player)->begin() != (*player)->end()) {
			monster = (*player)->begin();
			_monsterID_SKILL = (*monster)->GetID();
		}

		if (monster != (*player)->end())
			strcpy_s(_comboPreview, (*monster)->GetName());
	}

	char label[128] = { 0 };
	sprintf_s(label, "%s###DetailMonster", LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER").data());
	if(ImGui::BeginCombo(label, _comboPreview[0] ? _comboPreview : nullptr, ImGuiComboFlags_HeightLarge)) {

		sprintf_s(label, "%s##all", LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_ALL").data());
		if (ImGui::Selectable(label, _monsterFilter == MonsterFilter::All))
			_monsterFilter = MonsterFilter::All;

		sprintf_s(label, "%s##custom", LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_CUSTOM").data());
		if (ImGui::Selectable(label, _monsterFilter == MonsterFilter::Custom)) {

			// Start from everything checked so the list reads as a filter to
			// narrow down rather than an empty table to fill in.
			if (_customMonsters.empty())
				for (auto itr = (*player)->begin(); itr != (*player)->end(); itr++)
					_customMonsters.insert((*itr)->GetID());

			_monsterFilter = MonsterFilter::Custom;
			_customWindowOpen = true;
		}

		ImGui::Separator();

		for (auto itr = (*player)->begin(); itr != (*player)->end(); itr++)
		{

			char ext[MONSTER_NAME_LEN] = { 0 };
#ifdef _DEBUG
			sprintf_s(ext, "(%d)", (*itr)->GetDB2());
#endif

			char label[MONSTER_NAME_LEN + MONSTER_NAME_LEN] = { 0 };
			sprintf_s(label, MONSTER_NAME_LEN + MONSTER_NAME_LEN, "%s%s##%d", (*itr)->GetName(), ext, (*itr)->GetID());

			if (ImGui::Selectable(label, _monsterFilter == MonsterFilter::Single && _monsterID_SKILL == (*itr)->GetID())) {
				_monsterID_SKILL = (*itr)->GetID();
				_monsterFilter = MonsterFilter::Single;
			}
		}

		ImGui::EndCombo();
	}
}

void SpecificInformation::UpdateMonsterFilterWindow(int64_t index) {

	if (!_customWindowOpen || _monsterFilter != MonsterFilter::Custom)
		return;

	auto player = DAMAGEMETER.GetPlayerInfo(_playerID);

	if (player == DAMAGEMETER.end())
		return;

	char title[192] = { 0 };
	sprintf_s(title, 192, "%s %s###DetailMonsterFilter%lld", DAMAGEMETER.GetPlayerName(_playerID), LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_FILTER").data(), index);

	ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
	ImGui::Begin(title, &_customWindowOpen, ImGuiWindowFlags_None);
	{
		if (ImGui::Button(LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_FILTER_CHECK_ALL").data()))
			for (auto itr = (*player)->begin(); itr != (*player)->end(); itr++)
				_customMonsters.insert((*itr)->GetID());

		ImGui::SameLine();

		if (ImGui::Button(LANGMANAGER.GetText("STR_SPECIFICINFO_MONSTER_FILTER_UNCHECK_ALL").data()))
			_customMonsters.clear();

		ImGui::Separator();

		for (auto itr = (*player)->begin(); itr != (*player)->end(); itr++) {

			uint32_t monsterID = (*itr)->GetID();
			bool checked = _customMonsters.find(monsterID) != _customMonsters.end();

			char label[MONSTER_NAME_LEN + MONSTER_NAME_LEN] = { 0 };
			sprintf_s(label, MONSTER_NAME_LEN + MONSTER_NAME_LEN, "%s##filter%d", (*itr)->GetName(), monsterID);

			if (ImGui::Checkbox(label, &checked)) {
				if (checked)
					_customMonsters.insert(monsterID);
				else
					_customMonsters.erase(monsterID);
			}
		}
	}
	ImGui::End();
}

void SpecificInformation::UpdateSkillTable() {

	BuildSkillTotals();

	if (_skillTotals.empty())
		return;

	ImGuiStyle& style = ImGui::GetStyle();

	ImVec2 prevWindowPadding = style.WindowPadding;
	style.WindowPadding.x = 0;
	style.WindowPadding.y = 0;

	char table[128] = { 0 };
	sprintf_s(table, 128, "##skillbreakdowntable");
	if(ImGui::BeginTable(table, 8, ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | THEME.TableFlags())) {

		ImGui::SetWindowFontScale(_columnFontScale);

		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_NAME").data(), ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoClip | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DPS").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DAMAGE_PERCENT").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TOTAL_DAMAGE").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TOTAL_ALL_DAMAGE").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TOTAL_HIT").data(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_CRIT_RATE").data(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_HIT_PER_SECOND").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableHeadersRow();

		uint64_t max_Damage = _skillTotals.front()._damage;

		if (max_Damage == 0)
			max_Damage = 1;

		char comma[128] = { 0 }; char label[128] = { 0 };
		float windowWidth = ImGui::GetWindowWidth();

		ImGui::SetWindowFontScale(_tableFontScale);

		for (auto itr = _skillTotals.begin(); itr != _skillTotals.end(); itr++) {

			float damage_percent = static_cast<float>((double)itr->_damage / (double)max_Damage);

			if (damage_percent > 1)
				damage_percent = 1;
			else if (damage_percent < 0)
				damage_percent = 0;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			DrawBar(windowWidth, damage_percent, UIOPTION.GetJobColor(DAMAGEMETER.GetPlayerJob(_playerID)));

			// NAME
			ImGui::Text(itr->_name);

			ImGui::TableNextColumn();

			// DPS
			double dps = ((double)itr->_damage) / _tableTime;
			if (UIOPTION.is1K()) {
				dps /= 1000;
				sprintf_s(label, 128, "%.0lf", dps);
			}
			else if (UIOPTION.is1M()) {
				dps /= 1000000;
				sprintf_s(label, 128, "%.1lf", dps);
			}
			else if (UIOPTION.is10K()) {
				dps /= 10000;
				sprintf_s(label, 128, "%.0lf", dps);
			}
			if (UIOPTION.is1M())
				TextCommmaIncludeDecimal(dps, sizeof(comma), comma);
			else {
				sprintf_s(label, 128, "%.0lf", dps);
				TextCommma(label, comma);
			}
			if (UIOPTION.is1K())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
			else if (UIOPTION.is1M())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M").data());
			else if (UIOPTION.is10K())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
			ImGui::Text(comma);

			ImGui::TableNextColumn();

			// D%
			sprintf_s(label, 128, "%.0lf", _skillTotalsDamage == 0 ? 0.0 : ((double)itr->_damage / (double)_skillTotalsDamage * 100));
			ImGui::Text(label);

			ImGui::TableNextColumn();

			// DAMAGE (selected enemies)
			FormatDamage(itr->_damage, comma, sizeof(comma));
			ImGui::Text(comma);

			ImGui::TableNextColumn();

			// TOTAL (every enemy, selection ignored)
			FormatDamage(itr->_totalDamage, comma, sizeof(comma));
			ImGui::Text(comma);

			ImGui::TableNextColumn();

			// HIT
			sprintf_s(label, 128, "%u", itr->_hitCount);
			TextCommma(label, comma);
			ImGui::Text(comma);

			ImGui::TableNextColumn();

			// CRIT
			float crit = 0;

			if (itr->_hitCount != 0)
				crit = (float)itr->_critHitCount / (float)itr->_hitCount * 100;

			sprintf_s(label, 128, "%.0f", crit);
			ImGui::Text(label);

			ImGui::TableNextColumn();

			// HIT/S
			sprintf_s(label, 128, "%.2lf", (double)itr->_hitCount / _tableTime);
			ImGui::Text(label);

			ImGui::TableNextColumn();
		}

		ImGui::SetWindowFontScale(_globalFontScale);
		ImGui::EndTable();
	}

	style.WindowPadding.x = prevWindowPadding.x;
	style.WindowPadding.y = prevWindowPadding.y;

}

void SpecificInformation::UpdateBuffMeter() {

	BUFFMETER.GetLock();
	{
		char label[128] = { 0 };
		sprintf_s(label, "%s###DetailBuffDeBuff", LANGMANAGER.GetText("STR_SPECIFICINFO_BUFF_AND_DEBUFF").data());
		if (ImGui::BeginTabItem(label))
		{
			bool textEffect = THEME.PushTextEffect();
			ImGui::TextAlignCenter::SetTextAlignCenter();
			{
				UpdateBuffTable();
			}
			ImGui::TextAlignCenter::UnSetTextAlignCenter();
			THEME.PopTextEffect(textEffect);

			ImGui::EndTabItem();
		}
	}
	BUFFMETER.FreeLock();
}

void SpecificInformation::UpdateBuffTable() {

	auto buff = BUFFMETER.find(_playerID);

	if (buff == BUFFMETER.end())
		return;

	ImGuiStyle& style = ImGui::GetStyle();

	ImVec2 prevWindowPadding = style.WindowPadding;
	style.WindowPadding.x = 0;
	style.WindowPadding.y = 0;

	char table[128] = { 0 };
	sprintf_s(table, 128, "##bufftable");
	if (ImGui::BeginTable(table, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | THEME.TableFlags())) {

		ImGui::SetWindowFontScale(_columnFontScale);

		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_SPECIFICINFO_BUFF_NAME").data(), ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoClip | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DURATION").data(), ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableHeadersRow();

		ImGui::SetWindowFontScale(_tableFontScale);

		float windowWidth = ImGui::GetWindowWidth();
		for (auto itr = (*buff)->begin(); itr != (*buff)->end(); itr++) {

			char label[128] = { 0 };
			float duration_percent = (*itr)->GetTime() / (DAMAGEMETER.GetTime() / 1000);

			if (duration_percent > 1)
				duration_percent = 1;
			else if (duration_percent < 0)
				duration_percent = 0;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			DrawBar(windowWidth, duration_percent, UIOPTION.GetJobColor(DAMAGEMETER.GetPlayerJob(_playerID)));

			// NAME & DESC
			ImGui::TextAlignCenter::UnSetTextAlignCenter();
			{
				// Custom text align bug BRUH
				ImGui::SetCursorPosX((ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize((*itr)->GetName()).x - ImGui::GetScrollX()) * 0.5f);
				ImGui::Text((*itr)->GetName());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip((*itr)->GetDesc());
			}
			ImGui::TextAlignCenter::SetTextAlignCenter();
			ImGui::TableNextColumn();

			// DURATION
			sprintf_s(label, 128, "%.0f", duration_percent * 100);
			ImGui::Text(label);
		}

		ImGui::SetWindowFontScale(_globalFontScale);
		ImGui::EndTable();
	}

	style.WindowPadding.x = prevWindowPadding.x;
	style.WindowPadding.y = prevWindowPadding.y;
}

void SpecificInformation::DrawBar(float window_Width, float percent, ImU32 color) {

	THEME.DrawBar(window_Width, percent, color);

}